#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pj::engine {

class RawBuffer {
public:
  RawBuffer() = default;
  explicit RawBuffer(std::size_t initial_capacity);

  void reserve(std::size_t capacity);
  void append(const void* data, std::size_t size);
  void resize(std::size_t new_size);
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

void init(RawBuffer& buf, std::size_t num_bits);
void set_valid(RawBuffer& buf, std::size_t bit_index);
void set_null(RawBuffer& buf, std::size_t bit_index);
[[nodiscard]] bool is_valid(const RawBuffer& buf, std::size_t bit_index);
[[nodiscard]] std::size_t count_nulls(const RawBuffer& buf, std::size_t num_bits);

// Required bytes for num_bits bits (ceil division)
[[nodiscard]] constexpr std::size_t bytes_for_bits(std::size_t num_bits) noexcept {
  return (num_bits + 7) / 8;
}

}  // namespace validity_bitmap

}  // namespace pj::engine
