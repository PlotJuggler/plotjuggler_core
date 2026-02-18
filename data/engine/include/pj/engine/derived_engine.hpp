#pragma once
#include <memory>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "pj/base/expected.hpp"
#include "pj/base/span.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/column_buffer.hpp"

namespace pj::engine {

class DataEngine;

/// Implementation struct — defined in derived_engine.cpp, hidden from callers.
struct DerivedEngineImpl;

// ---------------------------------------------------------------------------
// VarValue — universal column value type for transform I/O
// ---------------------------------------------------------------------------
// Engine storage kinds map as follows:
//   kFloat32, kFloat64        → double  (float32 widens losslessly)
//   kInt8 … kInt64, kBool     → int64_t (sign-extend; bool → 0/1)
//   kUint64                   → int64_t (cast; caveat for values > INT64_MAX)
//   kString                   → std::string
using VarValue = std::variant<int64_t, double, std::string>;

// ---------------------------------------------------------------------------
// ISISOTransform — single-input / single-output transform
// ---------------------------------------------------------------------------
// SEQUENTIAL CONTRACT (fundamental):
//   The engine calls calculate() once per sample, strictly in ascending
//   timestamp order. Implementations may therefore accumulate state freely
//   in member variables between calls (e.g. previous value for derivative,
//   ring buffer for moving average, running sum for integral).
//   State persists across chunk boundaries — the engine never resets it
//   during incremental scheduling.
//   reset() is the only path that clears state; the engine calls it
//   exclusively before a full batch recompute.
class ISISOTransform {
 public:
  virtual ~ISISOTransform() = default;

  /// Clear all accumulated state. Called by DerivedEngine before batch recompute.
  /// After reset(), the next calculate() call must behave as if no data has
  /// been seen (same as a freshly constructed instance).
  virtual void reset() {}

  /// Declare the StorageKind of the output column. Called once at registration.
  /// Default: kFloat64 (suitable for most numeric filters).
  /// Override to preserve integer types or produce strings.
  virtual StorageKind output_kind(StorageKind input_kind) const {
    (void)input_kind;
    return StorageKind::kFloat64;
  }

  /// Process one sample. Called in strictly ascending timestamp order.
  ///   time:      sample timestamp (nanoseconds since epoch)
  ///   input:     sample value decoded as VarValue
  ///   out_time:  output timestamp (written by callee; read by engine only when true)
  ///   out_value: output value   (written by callee; read by engine only when true)
  ///
  /// Returns true to emit a row, false to suppress (e.g. first row of derivative).
  ///
  /// out_time MAY differ from `time` — time-offset transforms and interpolation
  /// may produce output on a different time grid than their input.
  /// When true is returned, out_time must be >= all previously returned out_times.
  virtual bool calculate(pj::Timestamp time, const VarValue& input,
                         pj::Timestamp& out_time, VarValue& out_value) = 0;
};

// ---------------------------------------------------------------------------
// IMIMOTransform — multi-input / multi-output transform
// ---------------------------------------------------------------------------
// SEQUENTIAL CONTRACT (fundamental, same as ISISOTransform):
//   The engine calls calculate() once per joined sample, strictly in ascending
//   timestamp order. State may be accumulated in member variables between calls.
//   reset() clears all state; called exclusively before batch recompute.
class IMIMOTransform {
 public:
  virtual ~IMIMOTransform() = default;

  /// Clear all accumulated state. Called by DerivedEngine before batch recompute.
  virtual void reset() {}

  /// Declare output StorageKind for each output topic.
  /// Called once at registration with the input kinds (one per input topic).
  /// Return one StorageKind per output topic name passed to add_mimo_transform.
  virtual std::vector<StorageKind> output_kinds(pj::Span<const StorageKind> input_kinds) const = 0;

  /// Process one joined sample. Called in strictly ascending timestamp order,
  /// only when ALL input topics have a sample at exactly `time`.
  ///   inputs[i]  = value from input topic i (in add_mimo_transform order).
  ///   out_time   = output timestamp (written by callee; read only when true).
  ///   output     = pre-allocated buffer (size == num output topics); fill in-place.
  ///                output[k] corresponds to output_topic_names[k] from add_mimo_transform.
  ///
  /// Returns true to emit a row; false to suppress.
  /// out_time MAY differ from `time`. When true is returned, out_time must be
  /// >= all previously returned out_times. All M output topics share this timestamp.
  virtual bool calculate(pj::Timestamp time, pj::Span<const VarValue> inputs,
                         pj::Timestamp& out_time, std::vector<VarValue>& output) = 0;
};

// ---------------------------------------------------------------------------
// DerivedEngine
// ---------------------------------------------------------------------------
class DerivedEngine {
 public:
  explicit DerivedEngine(DataEngine& engine);
  ~DerivedEngine();
  DerivedEngine(const DerivedEngine&) = delete;
  DerivedEngine& operator=(const DerivedEngine&) = delete;

  // ---- SISO ----------------------------------------------------------------
  // Creates one scalar output topic (StorageKind from op->output_kind()).
  // Returns error if:
  //   - input_topic_id does not exist
  //   - input topic has more than one column
  //   - output_topic_name already registered within output_dataset_id
  [[nodiscard]] pj::Expected<pj::NodeId> add_siso_transform(
      pj::TopicId input_topic_id, std::string output_topic_name, pj::DatasetId output_dataset_id,
      std::unique_ptr<ISISOTransform> op);

  // ---- MIMO (Phase 3) -------------------------------------------------------
  // All input topics must be single-column (scalar).
  // A row is emitted only when ALL input topics share the exact same timestamp.
  // Creates output_topic_names.size() new topics (kinds from op->output_kinds()).
  [[nodiscard]] pj::Expected<pj::NodeId> add_mimo_transform(
      std::vector<pj::TopicId> input_topic_ids, std::vector<std::string> output_topic_names,
      pj::DatasetId output_dataset_id, std::unique_ptr<IMIMOTransform> op);

  // ---- Node management -----------------------------------------------------
  pj::Status remove_node(pj::NodeId id);
  [[nodiscard]] bool has_node(pj::NodeId id) const noexcept;

  // Returns output topic IDs: 1 for SISO, M for MIMO.
  [[nodiscard]] std::vector<pj::TopicId> output_topics(pj::NodeId id) const;

  // Kahn's topological order (upstream → downstream).
  [[nodiscard]] std::vector<pj::NodeId> topological_order() const;

  // ---- Commit-cycle hook ---------------------------------------------------
  // Call after DataEngine::commit_chunks() with the set of changed topic IDs.
  // Marks directly dependent nodes dirty.
  void on_source_committed(pj::Span<const pj::TopicId> changed_topics);

  // ---- Scheduling ----------------------------------------------------------
  // Process all dirty nodes in topological order (incremental path).
  // If active_nodes is non-empty, only those nodes (and their transitive
  // upstream dependencies) are considered.
  pj::Status schedule(const std::unordered_set<pj::NodeId>& active_nodes = {});

  // Full history recompute: clear output, reset transform, replay all input.
  pj::Status recompute_batch(pj::NodeId node_id);

 private:
  DataEngine& engine_;
  pj::NodeId next_node_id_ = 1;
  std::unique_ptr<DerivedEngineImpl> impl_;
};

}  // namespace pj::engine
