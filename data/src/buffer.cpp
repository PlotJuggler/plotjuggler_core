#include "pj/engine/buffer.hpp"

#include <bit>
#include <cstring>

namespace pj::engine {

// ---------------------------------------------------------------------------
// RawBuffer
// ---------------------------------------------------------------------------

RawBuffer::RawBuffer(std::size_t initial_capacity) {
  data_.reserve(initial_capacity);
}

void RawBuffer::reserve(std::size_t capacity) { data_.reserve(capacity); }

void RawBuffer::append(const void* data, std::size_t size) {
  const auto* begin = static_cast<const uint8_t*>(data);
  data_.insert(data_.end(), begin, begin + size);
}

void RawBuffer::resize(std::size_t new_size) { data_.resize(new_size); }

void RawBuffer::clear() { data_.clear(); }

const uint8_t* RawBuffer::data() const noexcept { return data_.data(); }

uint8_t* RawBuffer::mutable_data() noexcept { return data_.data(); }

std::size_t RawBuffer::size() const noexcept { return data_.size(); }

std::size_t RawBuffer::capacity() const noexcept { return data_.capacity(); }

bool RawBuffer::empty() const noexcept { return data_.empty(); }

// ---------------------------------------------------------------------------
// Validity bitmap
// ---------------------------------------------------------------------------

namespace validity_bitmap {

void init(RawBuffer& buf, std::size_t num_bits) {
  buf.resize(bytes_for_bits(num_bits));
  std::memset(buf.mutable_data(), 0xFF, buf.size());
}

void set_valid(RawBuffer& buf, std::size_t bit_index) {
  buf.mutable_data()[bit_index / 8] |= static_cast<uint8_t>(1u << (bit_index % 8));
}

void set_null(RawBuffer& buf, std::size_t bit_index) {
  buf.mutable_data()[bit_index / 8] &=
      static_cast<uint8_t>(~(1u << (bit_index % 8)));
}

bool is_valid(const RawBuffer& buf, std::size_t bit_index) {
  return (buf.data()[bit_index / 8] & (1u << (bit_index % 8))) != 0;
}

std::size_t count_nulls(const RawBuffer& buf, std::size_t num_bits) {
  const std::size_t num_bytes = bytes_for_bits(num_bits);
  const uint8_t* ptr = buf.data();

  std::size_t total_set_bits = 0;

  // Process full bytes
  const std::size_t full_bytes = num_bits / 8;
  for (std::size_t i = 0; i < full_bytes; ++i) {
    total_set_bits += static_cast<std::size_t>(std::popcount(ptr[i]));
  }

  // Process remaining bits in the last partial byte (if any)
  const std::size_t remaining_bits = num_bits % 8;
  if (remaining_bits > 0 && num_bytes > 0) {
    const uint8_t mask = static_cast<uint8_t>((1u << remaining_bits) - 1u);
    total_set_bits +=
        static_cast<std::size_t>(std::popcount(static_cast<uint8_t>(ptr[full_bytes] & mask)));
  }

  return num_bits - total_set_bits;
}

}  // namespace validity_bitmap

}  // namespace pj::engine
