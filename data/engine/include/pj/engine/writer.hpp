#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/chunk.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/topic_storage.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::DatasetId;
using pj::FieldId;
using pj::NumericType;
using pj::NumericValue;
using pj::SchemaId;
using pj::Timestamp;
using pj::TopicId;
using pj::TypeTreeNode;
using pj::PrimitiveType;
using pj::TypeKind;

class DataEngine;  // forward declaration

/// Handle returned by bind_topic_writer for fast-path column access.
struct TopicWriteHandle {
  TopicId topic_id;
  std::vector<FieldId> field_ids;
};

/// Handle for scalar convenience API — single numeric column per topic.
struct ScalarSeriesHandle {
  TopicId topic_id;
  FieldId value_field;
};

/// Describes one column's data for bulk append_columns().
struct ColumnData {
  std::size_t col_index;

  StorageKind kind;
  const void* data = nullptr;
  std::size_t count = 0;

  // For strings: Arrow-compatible offset+data layout
  const uint32_t* string_offsets = nullptr;
  const char* string_data = nullptr;

  // Optional validity bitmap (nullptr = all valid)
  const uint8_t* validity_bitmap = nullptr;
  std::size_t validity_bit_offset = 0;

  static ColumnData Float32(std::size_t col, const float* values,
                            std::size_t n,
                            const uint8_t* validity = nullptr,
                            std::size_t bit_offset = 0) {
    return {col, StorageKind::kFloat32, values, n,
            nullptr, nullptr, validity, bit_offset};
  }
  static ColumnData Float64(std::size_t col, const double* values,
                            std::size_t n,
                            const uint8_t* validity = nullptr,
                            std::size_t bit_offset = 0) {
    return {col, StorageKind::kFloat64, values, n,
            nullptr, nullptr, validity, bit_offset};
  }
  static ColumnData Int32(std::size_t col, const int32_t* values,
                          std::size_t n,
                          const uint8_t* validity = nullptr,
                          std::size_t bit_offset = 0) {
    return {col, StorageKind::kInt32, values, n,
            nullptr, nullptr, validity, bit_offset};
  }
  static ColumnData Int64(std::size_t col, const int64_t* values,
                          std::size_t n,
                          const uint8_t* validity = nullptr,
                          std::size_t bit_offset = 0) {
    return {col, StorageKind::kInt64, values, n,
            nullptr, nullptr, validity, bit_offset};
  }
  static ColumnData Uint64(std::size_t col, const uint64_t* values,
                           std::size_t n,
                           const uint8_t* validity = nullptr,
                           std::size_t bit_offset = 0) {
    return {col, StorageKind::kUint64, values, n,
            nullptr, nullptr, validity, bit_offset};
  }
  static ColumnData Bool(std::size_t col, const uint8_t* values,
                         std::size_t n,
                         const uint8_t* validity = nullptr,
                         std::size_t bit_offset = 0) {
    return {col, StorageKind::kBool, values, n,
            nullptr, nullptr, validity, bit_offset};
  }
  static ColumnData String(std::size_t col, const uint32_t* offsets,
                           const char* str_data, std::size_t n,
                           const uint8_t* validity = nullptr,
                           std::size_t bit_offset = 0) {
    return {col, StorageKind::kString, nullptr, n,
            offsets, str_data, validity, bit_offset};
  }
};

class DataWriter {
 public:
  explicit DataWriter(DataEngine& engine);

  // ---- Schema registration (delegates to engine's TypeRegistry) ----
  [[nodiscard]] absl::StatusOr<SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree);

  // ---- Topic registration ----
  [[nodiscard]] absl::StatusOr<TopicId> register_topic(
      DatasetId dataset_id, TopicDescriptor descriptor);

  // ---- Bind for fast path ----
  [[nodiscard]] absl::StatusOr<TopicWriteHandle> bind_topic_writer(
      TopicId topic_id);

  // ---- Field resolution ----
  [[nodiscard]] absl::StatusOr<FieldId> resolve_field(
      TopicId topic_id, std::string_view field_path);

  // ---- Row-at-a-time append ----
  [[nodiscard]] absl::Status begin_row(TopicId topic_id, Timestamp t);
  void finish_row(TopicId topic_id);

  // ---- Set values for current row by column index (7 storage types) ----
  void set_float32(TopicId topic_id, std::size_t col_index, float value);
  void set_float64(TopicId topic_id, std::size_t col_index, double value);
  void set_int32(TopicId topic_id, std::size_t col_index, int32_t value);
  void set_int64(TopicId topic_id, std::size_t col_index, int64_t value);
  void set_uint64(TopicId topic_id, std::size_t col_index, uint64_t value);
  void set_string(TopicId topic_id, std::size_t col_index,
                  std::string_view value);
  void set_bool(TopicId topic_id, std::size_t col_index, bool value);
  void set_null(TopicId topic_id, std::size_t col_index);

  // ---- Bulk column append ----
  [[nodiscard]] absl::Status append_columns(
      TopicId topic_id, absl::Span<const Timestamp> timestamps,
      absl::Span<const ColumnData> columns);

  // ---- Scalar convenience API ----
  [[nodiscard]] absl::StatusOr<ScalarSeriesHandle> register_scalar_series(
      DatasetId dataset_id, std::string_view topic_name,
      NumericType value_type);
  void append_scalar(const ScalarSeriesHandle& handle, Timestamp t,
                     NumericValue value);

  // ---- Flush ----
  [[nodiscard]] std::vector<TopicChunk> flush(TopicId topic_id);
  [[nodiscard]] std::vector<std::pair<TopicId, TopicChunk>> flush_all();

 private:
  DataEngine& engine_;
  absl::flat_hash_map<TopicId, TopicChunkBuilder> builders_;
  absl::flat_hash_map<TopicId, std::vector<TopicChunk>> pending_chunks_;

  // Column descriptors cached per topic (needed to recreate builders)
  absl::flat_hash_map<TopicId, std::vector<ColumnDescriptor>>
      topic_columns_;

  TopicChunkBuilder& get_or_create_builder(TopicId topic_id);

  // Build column descriptors from a type tree
  static std::vector<ColumnDescriptor> build_column_descriptors(
      const TypeTreeNode& root);

  // Seal current builder and move chunk to pending list
  void auto_seal(TopicId topic_id);
};

}  // namespace pj::engine
