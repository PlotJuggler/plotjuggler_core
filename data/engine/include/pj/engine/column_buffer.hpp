#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/buffer.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::FieldId;
using pj::PrimitiveType;

// Physical storage category. Narrow integers (int8, int16) widen to int64;
// int32 is kept as a dedicated storage kind because it's extremely common.
// Narrow unsigned integers (uint8..uint32) widen to uint64.
// FOR compression recovers further byte savings at seal time.
enum class StorageKind : uint8_t {
  kFloat32,
  kFloat64,
  kInt32,
  kInt64,
  kUint64,
  kBool,
  kString,
};

[[nodiscard]] constexpr StorageKind storage_kind_of(
    PrimitiveType t) noexcept {
  switch (t) {
    case PrimitiveType::kFloat32: return StorageKind::kFloat32;
    case PrimitiveType::kFloat64: return StorageKind::kFloat64;
    case PrimitiveType::kInt8:
    case PrimitiveType::kInt16:   return StorageKind::kInt64;
    case PrimitiveType::kInt32:   return StorageKind::kInt32;
    case PrimitiveType::kInt64:   return StorageKind::kInt64;
    case PrimitiveType::kUint8:
    case PrimitiveType::kUint16:
    case PrimitiveType::kUint32:
    case PrimitiveType::kUint64:  return StorageKind::kUint64;
    case PrimitiveType::kBool:    return StorageKind::kBool;
    case PrimitiveType::kString:  return StorageKind::kString;
  }
  return StorageKind::kFloat64;
}

// Byte size of a StorageKind's fixed-width element. Returns 0 for kString.
[[nodiscard]] constexpr std::size_t storage_kind_size(
    StorageKind k) noexcept {
  switch (k) {
    case StorageKind::kFloat32: return sizeof(float);
    case StorageKind::kFloat64: return sizeof(double);
    case StorageKind::kInt32:   return sizeof(int32_t);
    case StorageKind::kInt64:   return sizeof(int64_t);
    case StorageKind::kUint64:  return sizeof(uint64_t);
    case StorageKind::kBool:    return sizeof(uint8_t);
    case StorageKind::kString:  return 0;
  }
  return 0;
}

enum class EncodingType : uint8_t {
  kRaw,              // Unencoded typed storage
  kDelta,            // Delta encoding (timestamps)
  kDictionary,       // Dictionary encoding (strings)
  kPackedBool,       // Packed bitfield (bools)
  kConstant,         // Single repeated value
  kFrameOfReference, // Min-subtracted narrowed offsets
};

struct ColumnDescriptor {
  FieldId field_id;
  PrimitiveType logical_type;  // Full logical type for metadata/schema
  std::string field_path;      // e.g., "position.x"
};

class TypedColumnBuffer {
 public:
  explicit TypedColumnBuffer(ColumnDescriptor descriptor);

  [[nodiscard]] const ColumnDescriptor& descriptor() const noexcept;
  [[nodiscard]] std::size_t row_count() const noexcept;
  [[nodiscard]] bool has_nulls() const noexcept;
  [[nodiscard]] bool is_valid(std::size_t row) const noexcept;

  // Append typed values (7 storage types)
  void append_float32(float value);
  void append_float64(double value);
  void append_int32(int32_t value);
  void append_int64(int64_t value);
  void append_uint64(uint64_t value);
  void append_bool(bool value);
  void append_string(std::string_view value);
  void append_null();

  // Read typed values (7 storage types)
  [[nodiscard]] float read_float32(std::size_t row) const;
  [[nodiscard]] double read_float64(std::size_t row) const;
  [[nodiscard]] int32_t read_int32(std::size_t row) const;
  [[nodiscard]] int64_t read_int64(std::size_t row) const;
  [[nodiscard]] uint64_t read_uint64(std::size_t row) const;
  [[nodiscard]] bool read_bool(std::size_t row) const;
  [[nodiscard]] std::string_view read_string(std::size_t row) const;
  [[nodiscard]] bool is_null(std::size_t row) const;

  // Read any numeric column as double (for stats, display).
  // For string columns, returns NaN.
  [[nodiscard]] double read_as_double(std::size_t row) const;

  // ---- Bulk append (contiguous memcpy-based) ----
  void append_float32_bulk(const float* data, std::size_t count);
  void append_float64_bulk(const double* data, std::size_t count);
  void append_int32_bulk(const int32_t* data, std::size_t count);
  void append_int64_bulk(const int64_t* data, std::size_t count);
  void append_uint64_bulk(const uint64_t* data, std::size_t count);
  void append_bool_bulk(const uint8_t* data, std::size_t count);

  /// Append strings from Arrow-compatible offset+data layout.
  /// offsets has (count + 1) entries; data contains the concatenated strings.
  void append_strings_bulk(const uint32_t* offsets, const char* data,
                           std::size_t count);

  /// Append a validity bitmap for the most recently appended `count` rows.
  /// Arrow-compatible bit layout. bit_offset is the starting bit within bitmap.
  void append_validity_bulk(const uint8_t* bitmap, std::size_t bit_offset,
                            std::size_t count);

  // Access underlying buffers (for encoding at seal time)
  [[nodiscard]] const RawBuffer& value_buffer() const noexcept;
  [[nodiscard]] const RawBuffer& validity_buffer() const noexcept;
  [[nodiscard]] const RawBuffer& offsets_buffer() const noexcept;  // strings only

 private:
  ColumnDescriptor descriptor_;
  RawBuffer values_;
  RawBuffer validity_;
  RawBuffer offsets_;  // For string: offset array (uint32_t per entry + 1 sentinel)
  std::size_t row_count_ = 0;
  std::size_t null_count_ = 0;
  bool validity_initialized_ = false;

  void ensure_validity_initialized();

  template <typename T>
  void append_fixed(T value);

  template <typename T>
  void append_fixed_bulk(const T* data, std::size_t count);

  template <typename T>
  [[nodiscard]] T read_fixed(std::size_t row) const;
};

}  // namespace pj::engine
