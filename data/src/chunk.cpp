#include "pj/engine/chunk.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace pj::engine {

namespace {

// Returns the byte size of a fixed-size PrimitiveType.
// For kString returns 0 (variable-length).
[[nodiscard]] constexpr std::size_t primitive_type_fixed_size(
    PrimitiveType type) noexcept {
  switch (type) {
    case PrimitiveType::kFloat32: return sizeof(float);
    case PrimitiveType::kFloat64: return sizeof(double);
    case PrimitiveType::kInt8:    return sizeof(int8_t);
    case PrimitiveType::kInt16:   return sizeof(int16_t);
    case PrimitiveType::kInt32:   return sizeof(int32_t);
    case PrimitiveType::kInt64:   return sizeof(int64_t);
    case PrimitiveType::kUint8:   return sizeof(uint8_t);
    case PrimitiveType::kUint16:  return sizeof(uint16_t);
    case PrimitiveType::kUint32:  return sizeof(uint32_t);
    case PrimitiveType::kUint64:  return sizeof(uint64_t);
    case PrimitiveType::kBool:    return sizeof(uint8_t);
    case PrimitiveType::kString:  return 0;
  }
  return 0;  // unreachable
}

// Read a numeric value from a raw buffer at a given row index,
// interpreting the data according to the logical type, and return as double.
[[nodiscard]] double read_raw_as_double(const RawBuffer& buf,
                                        PrimitiveType type,
                                        std::size_t row) {
  const std::size_t elem_size = primitive_type_fixed_size(type);
  const uint8_t* ptr = buf.data() + row * elem_size;

  switch (type) {
    case PrimitiveType::kFloat32: {
      float v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kFloat64: {
      double v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return v;
    }
    case PrimitiveType::kInt8: {
      int8_t v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kInt16: {
      int16_t v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kInt32: {
      int32_t v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kInt64: {
      int64_t v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kUint8: {
      uint8_t v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kUint16: {
      uint16_t v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kUint32: {
      uint32_t v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kUint64: {
      uint64_t v = 0;
      std::memcpy(&v, ptr, sizeof(v));
      return static_cast<double>(v);
    }
    case PrimitiveType::kBool:
    case PrimitiveType::kString:
      break;
  }
  return 0.0;  // unreachable for numeric types
}

}  // namespace

// ===========================================================================
// TopicChunkBuilder
// ===========================================================================

TopicChunkBuilder::TopicChunkBuilder(TopicId topic_id, SchemaId schema_id,
                                     std::vector<ColumnDescriptor> columns,
                                     uint32_t max_rows)
    : topic_id_(topic_id),
      schema_id_(schema_id),
      max_rows_(max_rows),
      column_descriptors_(std::move(columns)) {
  columns_.reserve(column_descriptors_.size());
  for (const auto& desc : column_descriptors_) {
    columns_.emplace_back(desc);
  }
  stats_.column_stats.resize(column_descriptors_.size());
}

void TopicChunkBuilder::begin_row(Timestamp timestamp) {
  row_in_progress_ = true;
  current_timestamp_ = timestamp;
  stats_.t_min = std::min(stats_.t_min, timestamp);
  stats_.t_max = std::max(stats_.t_max, timestamp);
}

// ---------------------------------------------------------------------------
// set_* methods
// ---------------------------------------------------------------------------

void TopicChunkBuilder::set_float32(std::size_t col_index, float value) {
  columns_[col_index].append_float32(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_float64(std::size_t col_index, double value) {
  columns_[col_index].append_float64(value);
  update_column_stats(col_index, value);
}

void TopicChunkBuilder::set_int8(std::size_t col_index, int8_t value) {
  columns_[col_index].append_int8(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_int16(std::size_t col_index, int16_t value) {
  columns_[col_index].append_int16(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_int32(std::size_t col_index, int32_t value) {
  columns_[col_index].append_int32(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_int64(std::size_t col_index, int64_t value) {
  columns_[col_index].append_int64(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_uint8(std::size_t col_index, uint8_t value) {
  columns_[col_index].append_uint8(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_uint16(std::size_t col_index, uint16_t value) {
  columns_[col_index].append_uint16(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_uint32(std::size_t col_index, uint32_t value) {
  columns_[col_index].append_uint32(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_uint64(std::size_t col_index, uint64_t value) {
  columns_[col_index].append_uint64(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_bool(std::size_t col_index, bool value) {
  columns_[col_index].append_bool(value);
  const double dval = value ? 1.0 : 0.0;
  update_column_stats(col_index, dval);
}

void TopicChunkBuilder::set_string(std::size_t col_index,
                                   std::string_view value) {
  columns_[col_index].append_string(value);
  // For strings we don't track numeric min/max, but we do track run_count
  // and is_constant.
  auto& cs = stats_.column_stats[col_index];
  const std::size_t current_row = columns_[col_index].row_count() - 1;
  if (current_row == 0) {
    cs.run_count = 1;
    // is_constant stays true
  } else {
    // Compare with previous string value
    std::string_view prev = columns_[col_index].read_string(current_row - 1);
    if (value != prev) {
      cs.is_constant = false;
      cs.run_count++;
    }
  }
}

void TopicChunkBuilder::set_null(std::size_t col_index) {
  columns_[col_index].append_null();
  stats_.column_stats[col_index].null_count++;
}

void TopicChunkBuilder::finish_row() {
  timestamps_.push_back(current_timestamp_);
  stats_.row_count++;
  row_in_progress_ = false;
}

bool TopicChunkBuilder::is_full() const noexcept {
  return row_count() >= max_rows_;
}

uint32_t TopicChunkBuilder::row_count() const noexcept {
  return stats_.row_count;
}

const ChunkStats& TopicChunkBuilder::stats() const noexcept {
  return stats_;
}

void TopicChunkBuilder::update_column_stats(std::size_t col_index,
                                            double value) {
  auto& cs = stats_.column_stats[col_index];
  const std::size_t current_row = columns_[col_index].row_count() - 1;

  // Update min/max
  if (!cs.min_value.has_value() || value < *cs.min_value) {
    cs.min_value = value;
  }
  if (!cs.max_value.has_value() || value > *cs.max_value) {
    cs.max_value = value;
  }

  // Update run_count / is_constant
  if (current_row == 0) {
    cs.run_count = 1;
    // is_constant stays true
  } else {
    double prev = columns_[col_index].read_as_double(current_row - 1);
    if (value != prev) {
      cs.is_constant = false;
      cs.run_count++;
    }
  }
}

// ---------------------------------------------------------------------------
// seal
// ---------------------------------------------------------------------------

TopicChunk TopicChunkBuilder::seal() {
  TopicChunk chunk;
  chunk.id = next_chunk_id_++;
  chunk.topic_id = topic_id_;
  chunk.schema_version = schema_id_;
  chunk.stats = stats_;
  chunk.column_descriptors = column_descriptors_;

  // Delta-encode timestamps
  chunk.encoded_timestamps = encoding::delta_encode(
      timestamps_.data(), timestamps_.size());

  const std::size_t num_cols = columns_.size();
  chunk.encoded_columns.resize(num_cols);
  chunk.column_encodings.resize(num_cols);
  chunk.validity_bitmaps.resize(num_cols);
  chunk.dictionary_data.resize(num_cols, std::nullopt);
  chunk.packed_bool_data.resize(num_cols, std::nullopt);

  for (std::size_t i = 0; i < num_cols; ++i) {
    const auto& col = columns_[i];
    const PrimitiveType ltype = column_descriptors_[i].logical_type;

    switch (ltype) {
      case PrimitiveType::kString: {
        // Dictionary-encode the string column
        chunk.column_encodings[i] = EncodingType::kDictionary;
        chunk.dictionary_data[i] = encoding::dictionary_encode_strings(
            col.offsets_buffer().data(), col.offsets_buffer().size(),
            col.value_buffer().data(), col.value_buffer().size(),
            col.row_count());
        break;
      }
      case PrimitiveType::kBool: {
        // Pack bools into a bitfield
        chunk.column_encodings[i] = EncodingType::kPackedBool;
        chunk.packed_bool_data[i] = encoding::pack_bools(
            col.value_buffer().data(), col.row_count());
        break;
      }
      default: {
        // Numeric types: copy raw value buffer
        chunk.column_encodings[i] = EncodingType::kRaw;
        chunk.encoded_columns[i].append(
            col.value_buffer().data(), col.value_buffer().size());
        break;
      }
    }

    // Copy validity bitmap if the column has nulls
    if (col.has_nulls()) {
      chunk.validity_bitmaps[i].append(
          col.validity_buffer().data(), col.validity_buffer().size());
    }
  }

  return chunk;
}

// ===========================================================================
// TopicChunk decode helpers
// ===========================================================================

Timestamp TopicChunk::read_timestamp(std::size_t row) const {
  int64_t result = encoded_timestamps.base_value;
  const uint8_t* delta_data = encoded_timestamps.deltas.data();
  for (std::size_t i = 0; i < row; ++i) {
    int64_t delta = 0;
    std::memcpy(&delta, delta_data + i * sizeof(int64_t), sizeof(int64_t));
    result += delta;
  }
  return result;
}

double TopicChunk::read_numeric_as_double(std::size_t col_index,
                                          std::size_t row) const {
  const PrimitiveType ltype = column_descriptors[col_index].logical_type;
  return read_raw_as_double(encoded_columns[col_index], ltype, row);
}

std::string_view TopicChunk::read_string(std::size_t col_index,
                                         std::size_t row) const {
  return encoding::dictionary_lookup(*dictionary_data[col_index], row);
}

bool TopicChunk::read_bool(std::size_t col_index, std::size_t row) const {
  return encoding::unpack_bool(*packed_bool_data[col_index], row);
}

bool TopicChunk::is_null(std::size_t col_index, std::size_t row) const {
  if (validity_bitmaps[col_index].empty()) {
    return false;
  }
  return !validity_bitmap::is_valid(validity_bitmaps[col_index], row);
}

}  // namespace pj::engine
