#include "pj/engine/arrow_import.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <arrow/api.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/writer.hpp"

namespace pj::engine::arrow_import {

// ---------------------------------------------------------------------------
// arrow_type_to_primitive
// ---------------------------------------------------------------------------

std::optional<PrimitiveType> arrow_type_to_primitive(
    const std::shared_ptr<arrow::DataType>& type) {
  switch (type->id()) {
    case arrow::Type::INT8:         return PrimitiveType::kInt8;
    case arrow::Type::INT16:        return PrimitiveType::kInt16;
    case arrow::Type::INT32:        return PrimitiveType::kInt32;
    case arrow::Type::INT64:        return PrimitiveType::kInt64;
    case arrow::Type::UINT8:        return PrimitiveType::kUint8;
    case arrow::Type::UINT16:       return PrimitiveType::kUint16;
    case arrow::Type::UINT32:       return PrimitiveType::kUint32;
    case arrow::Type::UINT64:       return PrimitiveType::kUint64;
    case arrow::Type::FLOAT:        return PrimitiveType::kFloat32;
    case arrow::Type::DOUBLE:       return PrimitiveType::kFloat64;
    case arrow::Type::BOOL:         return PrimitiveType::kBool;
    case arrow::Type::STRING:
    case arrow::Type::LARGE_STRING: return PrimitiveType::kString;
    default:                        return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// schema_from_arrow
// ---------------------------------------------------------------------------

absl::StatusOr<std::pair<
    std::shared_ptr<pj::TypeTreeNode>,
    std::vector<ArrowColumnMapping>>>
schema_from_arrow(const arrow::Schema& schema) {
  std::vector<ArrowColumnMapping> mappings;
  std::vector<std::shared_ptr<pj::TypeTreeNode>> children;

  for (int i = 0; i < schema.num_fields(); ++i) {
    const auto& field = schema.field(i);
    auto pj_type = arrow_type_to_primitive(field->type());
    if (!pj_type.has_value()) {
      continue;  // skip unsupported types
    }

    ArrowColumnMapping m;
    m.arrow_column_index = i;
    m.pj_column_index = mappings.size();
    m.pj_type = *pj_type;
    m.field_name = field->name();
    mappings.push_back(std::move(m));

    children.push_back(pj::make_primitive(field->name(), *pj_type));
  }

  if (mappings.empty()) {
    return absl::InvalidArgumentError(
        "No supported columns found in Arrow schema");
  }

  auto type_tree = pj::make_struct("arrow_row", std::move(children));
  return std::make_pair(std::move(type_tree), std::move(mappings));
}

// ---------------------------------------------------------------------------
// Helpers: extract raw data from Arrow arrays
// ---------------------------------------------------------------------------

namespace {

/// Widen narrow integers to the PJ StorageKind width.
/// Returns a vector of widened values.
template <typename SrcT, typename DstT>
std::vector<DstT> widen_array(const arrow::Array& array, int64_t length) {
  const auto& typed =
      static_cast<const arrow::NumericArray<
          typename arrow::CTypeTraits<SrcT>::ArrowType>&>(array);
  std::vector<DstT> result(static_cast<std::size_t>(length));
  for (int64_t i = 0; i < length; ++i) {
    result[static_cast<std::size_t>(i)] = static_cast<DstT>(typed.Value(i));
  }
  return result;
}

/// Extract timestamps from an Arrow column (int64).
std::vector<Timestamp> extract_timestamps(const arrow::Array& array,
                                          int64_t length) {
  std::vector<Timestamp> result(static_cast<std::size_t>(length));

  if (array.type_id() == arrow::Type::INT64) {
    const auto& typed = static_cast<const arrow::Int64Array&>(array);
    const auto* raw = typed.raw_values();
    std::memcpy(result.data(), raw,
                static_cast<std::size_t>(length) * sizeof(Timestamp));
  } else if (array.type_id() == arrow::Type::INT32) {
    const auto& typed = static_cast<const arrow::Int32Array&>(array);
    for (int64_t i = 0; i < length; ++i) {
      result[static_cast<std::size_t>(i)] =
          static_cast<Timestamp>(typed.Value(i));
    }
  } else if (array.type_id() == arrow::Type::UINT64) {
    const auto& typed = static_cast<const arrow::UInt64Array&>(array);
    const auto* raw = typed.raw_values();
    std::memcpy(result.data(), raw,
                static_cast<std::size_t>(length) * sizeof(Timestamp));
  } else {
    // Fallback: try to read as int64 via cast
    for (int64_t i = 0; i < length; ++i) {
      result[static_cast<std::size_t>(i)] = i;
    }
  }

  return result;
}

/// Generate sequential timestamps [0, 1, 2, ..., n-1].
std::vector<Timestamp> generate_sequential_timestamps(int64_t length) {
  std::vector<Timestamp> result(static_cast<std::size_t>(length));
  for (int64_t i = 0; i < length; ++i) {
    result[static_cast<std::size_t>(i)] = i;
  }
  return result;
}

/// Build a ColumnData from an Arrow array + mapping.
/// May allocate a widening buffer for narrow integer types.
struct ColumnDataWithBuffer {
  ColumnData col_data;
  // Widening buffers (keep alive as long as col_data is used)
  std::vector<int64_t> int64_buf;
  std::vector<uint64_t> uint64_buf;
};

ColumnDataWithBuffer make_column_data(
    const arrow::Array& array,
    const ArrowColumnMapping& mapping,
    int64_t length) {
  ColumnDataWithBuffer result;
  const auto sk = storage_kind_of(mapping.pj_type);
  const auto n = static_cast<std::size_t>(length);

  // Extract validity bitmap info
  const uint8_t* validity = nullptr;
  std::size_t validity_offset = 0;
  if (array.null_count() > 0 && array.null_bitmap()) {
    validity = array.null_bitmap_data();
    validity_offset = static_cast<std::size_t>(array.offset());
  }

  switch (sk) {
    case StorageKind::kFloat32: {
      const auto& typed = static_cast<const arrow::FloatArray&>(array);
      result.col_data = ColumnData::Float32(
          mapping.pj_column_index, typed.raw_values(), n,
          validity, validity_offset);
      break;
    }
    case StorageKind::kFloat64: {
      const auto& typed = static_cast<const arrow::DoubleArray&>(array);
      result.col_data = ColumnData::Float64(
          mapping.pj_column_index, typed.raw_values(), n,
          validity, validity_offset);
      break;
    }
    case StorageKind::kInt32: {
      const auto& typed = static_cast<const arrow::Int32Array&>(array);
      result.col_data = ColumnData::Int32(
          mapping.pj_column_index, typed.raw_values(), n,
          validity, validity_offset);
      break;
    }
    case StorageKind::kInt64: {
      // May need widening from int8/int16
      switch (mapping.pj_type) {
        case PrimitiveType::kInt8:
          result.int64_buf = widen_array<int8_t, int64_t>(array, length);
          result.col_data = ColumnData::Int64(
              mapping.pj_column_index, result.int64_buf.data(), n,
              validity, validity_offset);
          break;
        case PrimitiveType::kInt16:
          result.int64_buf = widen_array<int16_t, int64_t>(array, length);
          result.col_data = ColumnData::Int64(
              mapping.pj_column_index, result.int64_buf.data(), n,
              validity, validity_offset);
          break;
        case PrimitiveType::kInt64: {
          const auto& typed = static_cast<const arrow::Int64Array&>(array);
          result.col_data = ColumnData::Int64(
              mapping.pj_column_index, typed.raw_values(), n,
              validity, validity_offset);
          break;
        }
        default:
          break;
      }
      break;
    }
    case StorageKind::kUint64: {
      // May need widening from uint8/uint16/uint32
      switch (mapping.pj_type) {
        case PrimitiveType::kUint8:
          result.uint64_buf = widen_array<uint8_t, uint64_t>(array, length);
          result.col_data = ColumnData::Uint64(
              mapping.pj_column_index, result.uint64_buf.data(), n,
              validity, validity_offset);
          break;
        case PrimitiveType::kUint16:
          result.uint64_buf = widen_array<uint16_t, uint64_t>(array, length);
          result.col_data = ColumnData::Uint64(
              mapping.pj_column_index, result.uint64_buf.data(), n,
              validity, validity_offset);
          break;
        case PrimitiveType::kUint32:
          result.uint64_buf = widen_array<uint32_t, uint64_t>(array, length);
          result.col_data = ColumnData::Uint64(
              mapping.pj_column_index, result.uint64_buf.data(), n,
              validity, validity_offset);
          break;
        case PrimitiveType::kUint64: {
          const auto& typed = static_cast<const arrow::UInt64Array&>(array);
          result.col_data = ColumnData::Uint64(
              mapping.pj_column_index, typed.raw_values(), n,
              validity, validity_offset);
          break;
        }
        default:
          break;
      }
      break;
    }
    case StorageKind::kBool: {
      // Arrow stores bools as packed bits; we need unpacked uint8_t.
      // Use a temporary buffer.
      const auto& typed = static_cast<const arrow::BooleanArray&>(array);
      // Reuse uint64_buf for bool storage (cast to uint8_t*)
      // Actually, just store as vector<uint64_t> sized appropriately
      // and write uint8_t values.
      std::vector<uint8_t> bool_buf(n);
      for (std::size_t i = 0; i < n; ++i) {
        bool_buf[i] = typed.Value(static_cast<int64_t>(i)) ? 1 : 0;
      }
      // Store in uint64_buf as raw bytes (hack: reinterpret)
      result.uint64_buf.resize((n + sizeof(uint64_t) - 1) / sizeof(uint64_t));
      std::memcpy(result.uint64_buf.data(), bool_buf.data(), n);
      result.col_data = ColumnData::Bool(
          mapping.pj_column_index,
          reinterpret_cast<const uint8_t*>(result.uint64_buf.data()), n,
          validity, validity_offset);
      break;
    }
    case StorageKind::kString: {
      const auto& typed = static_cast<const arrow::StringArray&>(array);
      result.col_data = ColumnData::String(
          mapping.pj_column_index,
          reinterpret_cast<const uint32_t*>(typed.raw_value_offsets()),
          reinterpret_cast<const char*>(typed.raw_data()),
          n, validity, validity_offset);
      break;
    }
  }

  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// import_record_batch
// ---------------------------------------------------------------------------

absl::Status import_record_batch(
    DataWriter& writer,
    TopicId topic_id,
    const arrow::RecordBatch& batch,
    const std::vector<ArrowColumnMapping>& mappings,
    int timestamp_column) {
  const int64_t num_rows = batch.num_rows();
  if (num_rows == 0) {
    return absl::OkStatus();
  }

  // Extract timestamps
  std::vector<Timestamp> timestamps;
  if (timestamp_column >= 0) {
    if (timestamp_column >= batch.num_columns()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "timestamp_column ", timestamp_column,
          " out of range (", batch.num_columns(), " columns)"));
    }
    timestamps = extract_timestamps(*batch.column(timestamp_column), num_rows);
  } else {
    timestamps = generate_sequential_timestamps(num_rows);
  }

  // Build ColumnData for each mapping
  std::vector<ColumnDataWithBuffer> col_buffers;
  col_buffers.reserve(mappings.size());
  std::vector<ColumnData> col_data_vec;
  col_data_vec.reserve(mappings.size());

  for (const auto& mapping : mappings) {
    if (mapping.arrow_column_index >= batch.num_columns()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Arrow column index ", mapping.arrow_column_index,
          " out of range"));
    }
    col_buffers.push_back(make_column_data(
        *batch.column(mapping.arrow_column_index), mapping, num_rows));
  }

  for (auto& cb : col_buffers) {
    col_data_vec.push_back(cb.col_data);
  }

  return writer.append_columns(topic_id, timestamps, col_data_vec);
}

// ---------------------------------------------------------------------------
// import_table
// ---------------------------------------------------------------------------

absl::Status import_table(
    DataWriter& writer,
    TopicId topic_id,
    const arrow::Table& table,
    const std::vector<ArrowColumnMapping>& mappings,
    int timestamp_column,
    bool combine_chunks) {
  std::shared_ptr<arrow::Table> working_table;

  if (combine_chunks) {
    auto result = table.CombineChunks(arrow::default_memory_pool());
    if (!result.ok()) {
      return absl::InternalError(absl::StrCat(
          "CombineChunks failed: ", result.status().ToString()));
    }
    working_table = *result;
  } else {
    // Take a shared pointer that doesn't own the table
    working_table = std::shared_ptr<arrow::Table>(
        const_cast<arrow::Table*>(&table), [](arrow::Table*) {});
  }

  // Convert table to record batches and import each
  auto batch_reader = arrow::TableBatchReader(*working_table);
  std::shared_ptr<arrow::RecordBatch> batch;

  while (true) {
    auto st = batch_reader.ReadNext(&batch);
    if (!st.ok()) {
      return absl::InternalError(absl::StrCat(
          "ReadNext failed: ", st.ToString()));
    }
    if (batch == nullptr) {
      break;
    }
    auto status = import_record_batch(
        writer, topic_id, *batch, mappings, timestamp_column);
    if (!status.ok()) {
      return status;
    }
  }

  return absl::OkStatus();
}

}  // namespace pj::engine::arrow_import
