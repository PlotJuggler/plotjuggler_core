#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {

struct ObjectTopicId {
  uint32_t id = 0;

  bool operator==(const ObjectTopicId& other) const {
    return id == other.id;
  }
  bool operator!=(const ObjectTopicId& other) const {
    return id != other.id;
  }
};

struct ObjectTopicDescriptor {
  DatasetId dataset_id = 0;
  std::string topic_name;
  std::string metadata_json;
};

/// Eager payload: store-owned bytes, counted against the retention budget.
using SharedBuffer = std::shared_ptr<const std::vector<uint8_t>>;

/// Lazy payload: idempotent, thread-safe fetcher returning bytes + anchor.
/// Invoked on every read; bytes are not counted against the retention budget.
using LazyCallback = std::function<sdk::PayloadView()>;

struct ObjectEntry {
  Timestamp timestamp = 0;
  // Holds either a SharedBuffer (eager owned payload, counted against the
  // retention budget) or a LazyCallback (lazy resolver). resolveEntry
  // discriminates via std::get_if; the variant is exhaustive over the two
  // (and only two) payload kinds.
  std::variant<SharedBuffer, LazyCallback> payload;
};

struct ResolvedObjectEntry {
  Timestamp timestamp = 0;
  // PayloadView lets the entry hold an opaque anchor (any shared_ptr<T>) plus
  // a non-owning Span over the bytes. Consumers read `payload.bytes` for the
  // data; they retain `payload.anchor` if they need the bytes to outlive the
  // resolve call. Both `pushOwned` and `pushLazy` paths land here:
  //   - owned:  payload.bytes spans the shared_ptr<vector<uint8_t>>; anchor IS that shared_ptr.
  //   - lazy:   payload is whatever the closure returns; anchor can be any shared_ptr<T>
  //             (e.g. a C-ABI payload anchor wrapped as shared_ptr<void>).
  // resolveEntry never casts the anchor to a concrete type; the type erasure
  // stays opaque all the way to the consumer.
  sdk::PayloadView payload;
};

struct RetentionBudget {
  int64_t time_window_ns = 0;
  size_t max_memory_bytes = 0;
};

class EntryTimestampsView {
 public:
  EntryTimestampsView() = default;
  EntryTimestampsView(std::shared_lock<std::shared_mutex> lock, const std::vector<Timestamp>* timestamps)
      : lock_(std::move(lock)), timestamps_(timestamps) {}

  [[nodiscard]] bool empty() const {
    return timestamps_ == nullptr || timestamps_->empty();
  }
  [[nodiscard]] size_t size() const {
    return timestamps_ != nullptr ? timestamps_->size() : 0;
  }
  [[nodiscard]] Timestamp operator[](size_t i) const {
    return (*timestamps_)[i];
  }
  [[nodiscard]] const Timestamp* begin() const {
    return timestamps_ != nullptr ? timestamps_->data() : nullptr;
  }
  [[nodiscard]] const Timestamp* end() const {
    return timestamps_ != nullptr ? timestamps_->data() + timestamps_->size() : nullptr;
  }

 private:
  std::shared_lock<std::shared_mutex> lock_;
  const std::vector<Timestamp>* timestamps_ = nullptr;
};

class ObjectStore {
 public:
  ObjectStore() = default;
  ~ObjectStore() = default;

  ObjectStore(const ObjectStore&) = delete;
  ObjectStore& operator=(const ObjectStore&) = delete;
  ObjectStore(ObjectStore&&) = delete;
  ObjectStore& operator=(ObjectStore&&) = delete;

  // --- Registration ---

  Expected<ObjectTopicId> registerTopic(const ObjectTopicDescriptor& descriptor);

  // Resolve a topic id by (dataset_id, topic_name) without registering. Returns
  // nullopt if no topic with that key exists. Used by hosts that need to bind a
  // parser-side write surface to a topic the source already registered.
  std::optional<ObjectTopicId> findTopic(DatasetId dataset_id, std::string_view topic_name) const;

  const ObjectTopicDescriptor& descriptor(ObjectTopicId id) const;

  std::vector<ObjectTopicId> listTopics() const;
  std::vector<ObjectTopicId> listTopics(DatasetId dataset_id) const;

  // --- Write ---

  Status pushOwned(ObjectTopicId id, Timestamp timestamp, std::vector<uint8_t> payload);

  // Fetcher runs on every read. Producers anchor on whatever owns the bytes
  // (chunk cache, mmap, fresh allocation); the store never copies — it just
  // retains the anchor through PayloadView. When the producer already holds
  // the bytes behind a shared_ptr (e.g. a streaming buffer handed off between
  // stores), the closure captures it and returns a view backed by it.
  Status pushLazy(ObjectTopicId id, Timestamp timestamp, LazyCallback fetch);

  // --- Read ---

  std::optional<ResolvedObjectEntry> latestAt(ObjectTopicId id, Timestamp timestamp) const;

  std::optional<ResolvedObjectEntry> at(ObjectTopicId id, size_t index) const;

  std::optional<size_t> indexAt(ObjectTopicId id, Timestamp timestamp) const;

  size_t entryCount(ObjectTopicId id) const;

  std::pair<Timestamp, Timestamp> timeRange(ObjectTopicId id) const;

  EntryTimestampsView entryTimestamps(ObjectTopicId id) const;

  // --- Retention ---

  void setRetentionBudget(ObjectTopicId id, RetentionBudget budget);
  RetentionBudget retentionBudget(ObjectTopicId id) const;
  size_t memoryUsage(ObjectTopicId id) const;

  // --- Explicit eviction ---

  void evictBefore(ObjectTopicId id, Timestamp threshold);
  void evictAllBefore(Timestamp threshold);

  // --- Cross-store flush ---

  // Move every entry from this store into `dst`, leaving this store empty
  // (topic registrations are preserved). Topics are matched by descriptor
  // (dataset_id + topic_name); both stores must have registered the same
  // descriptors or the call fails without partial mutation. For each series,
  // monotonicity is enforced strictly: the earliest timestamp being moved
  // must be greater than or equal to the destination's current last
  // timestamp. On any validation failure the call returns an error and
  // neither store is mutated.
  //
  // Zero-copy on the payload bytes. Each ObjectEntry is moved into the
  // destination's series by value; the std::variant inside holds either a
  // shared_ptr or a std::function, and moving it is a pointer/buffer move —
  // bytes captured by the closure or owned by the shared_ptr are never
  // copied or materialized during the flush. Lazy
  // entries preserve their semantics in the destination: their closure is
  // re-invoked only when the destination is read.
  //
  // After the move, the destination's retention budget is applied to each
  // touched series in normal order.
  Expected<void, std::string> flushTo(ObjectStore& dst);

  // --- Lifecycle ---

  void removeTopic(ObjectTopicId id);
  void clear();

 private:
  struct ObjectSeries {
    ObjectTopicDescriptor descriptor;
    std::deque<ObjectEntry> entries;
    std::vector<Timestamp> entry_timestamps;
    RetentionBudget budget;
    size_t memory_bytes = 0;
    mutable std::shared_mutex mutex;
  };

  ObjectSeries* findSeries(ObjectTopicId id);
  const ObjectSeries* findSeries(ObjectTopicId id) const;

  static std::optional<size_t> upperBoundIndex(const std::vector<Timestamp>& timestamps, Timestamp ts);
  static ResolvedObjectEntry resolveEntry(const ObjectEntry& entry);

  void evictFront(ObjectSeries& series);
  void applyRetention(ObjectSeries& series, Timestamp newest_ts);

  mutable std::shared_mutex store_mutex_;
  std::vector<std::pair<ObjectTopicId, std::unique_ptr<ObjectSeries>>> topics_;
  uint32_t next_id_ = 1;
};

}  // namespace PJ
