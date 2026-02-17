#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pj::engine {

/// Growable byte buffer used by column/chunk encodings.
class RawBuffer {
 public:
  /// Construct an empty buffer.
  RawBuffer() = default;
  /// Construct an empty buffer reserving `initial_capacity` bytes.
  explicit RawBuffer(std::size_t initial_capacity);

  /// Ensure the buffer can hold at least `capacity` bytes without reallocation.
  void reserve(std::size_t capacity);
  /// Append `size` bytes from `data`.
  void append(const void* data, std::size_t size);
  /// Resize to exactly `new_size` bytes.
  void resize(std::size_t new_size);
  /// Reset size to zero, preserving capacity.
  void clear();

  [[nodiscard]] const uint8_t* data() const noexcept;
  [[nodiscard]] uint8_t* mutable_data() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t capacity() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

 private:
  std::vector<uint8_t> data_;
};

// Validity bitmap operations (Arrow-compatible: bit index = row index)
namespace validity_bitmap {

/// Initialize bitmap with `num_bits` bits, all marked valid.
void init(RawBuffer& buf, std::size_t num_bits);
/// Mark one bit as valid.
void set_valid(RawBuffer& buf, std::size_t bit_index);
/// Mark one bit as null.
void set_null(RawBuffer& buf, std::size_t bit_index);
/// Return true if bit is valid.
[[nodiscard]] bool is_valid(const RawBuffer& buf, std::size_t bit_index);
/// Count null bits in the first `num_bits` bits.
[[nodiscard]] std::size_t count_nulls(const RawBuffer& buf, std::size_t num_bits);

// Required bytes for num_bits bits (ceil division)
[[nodiscard]] constexpr std::size_t bytes_for_bits(std::size_t num_bits) noexcept {
  return (num_bits + 7) / 8;
}

}  // namespace validity_bitmap

}  // namespace pj::engine
