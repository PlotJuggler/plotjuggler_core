#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "pj/base/assert.hpp"

namespace pj {

/// Minimal non-owning contiguous view.
template <typename T>
class Span {
 public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using size_type = std::size_t;
  using pointer = T*;
  using reference = T&;
  using iterator = T*;

  /// Construct an empty span.
  constexpr Span() noexcept : data_(nullptr), size_(0) {}

  /// Construct from pointer and element count.
  constexpr Span(pointer data, size_type size) noexcept : data_(data), size_(size) {}

  /// Construct from C array.
  template <std::size_t N>
  constexpr Span(element_type (&arr)[N]) noexcept : data_(arr), size_(N) {}

  /// Construct from mutable std::array.
  template <typename U, std::size_t N, typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
  constexpr Span(std::array<U, N>& arr) noexcept : data_(arr.data()), size_(N) {}

  /// Construct from const std::array.
  template <typename U, std::size_t N, typename = std::enable_if_t<std::is_convertible_v<const U (*)[], T (*)[]>>>
  constexpr Span(const std::array<U, N>& arr) noexcept : data_(arr.data()), size_(N) {}

  /// Construct from mutable std::vector.
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
  constexpr Span(std::vector<U>& v) noexcept : data_(v.data()), size_(v.size()) {}

  /// Construct from const std::vector.
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<const U (*)[], T (*)[]>>>
  constexpr Span(const std::vector<U>& v) noexcept : data_(v.data()), size_(v.size()) {}

  [[nodiscard]] constexpr pointer data() const noexcept {
    return data_;
  }
  [[nodiscard]] constexpr size_type size() const noexcept {
    return size_;
  }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] constexpr reference operator[](size_type idx) const {
    PJ_ASSERT(idx < size_, "Span index out of bounds");
    return data_[idx];
  }

  [[nodiscard]] constexpr reference front() const {
    PJ_ASSERT(size_ > 0, "Span is empty");
    return data_[0];
  }

  [[nodiscard]] constexpr reference back() const {
    PJ_ASSERT(size_ > 0, "Span is empty");
    return data_[size_ - 1];
  }

  [[nodiscard]] constexpr iterator begin() const noexcept {
    return data_;
  }
  [[nodiscard]] constexpr iterator end() const noexcept {
    return data_ + size_;
  }

  /// Return a bounded view starting at `offset` with `count` elements.
  [[nodiscard]] constexpr Span<T> subspan(size_type offset, size_type count) const noexcept {
    PJ_ASSERT(offset <= size_, "Span subspan offset out of bounds");
    PJ_ASSERT(offset + count <= size_, "Span subspan range out of bounds");
    return Span<T>(data_ + offset, count);
  }

 private:
  pointer data_;
  size_type size_;
};

/// Bit-level view over a byte buffer.
struct BitSpan {
  /// Backing bytes containing packed bits (LSB first).
  Span<const uint8_t> bytes;
  /// Bit index in `bytes` where this view starts.
  std::size_t bit_offset = 0;
  /// Number of readable bits in the view.
  std::size_t bit_length = 0;

  [[nodiscard]] constexpr bool empty() const noexcept {
    return bit_length == 0;
  }

  [[nodiscard]] constexpr std::size_t size_bits() const noexcept {
    return bit_length;
  }

  /// Read one bit at relative index `i`.
  [[nodiscard]] constexpr bool test(std::size_t i) const {
    PJ_ASSERT(i < bit_length, "BitSpan index out of bounds");
    const std::size_t bit = bit_offset + i;
    const uint8_t byte = bytes[bit / 8];
    return (byte & (1u << (bit % 8))) != 0;
  }

  /// Return a bit-range view relative to this span.
  [[nodiscard]] constexpr BitSpan subspan(std::size_t offset_bits, std::size_t count_bits) const {
    PJ_ASSERT(offset_bits <= bit_length, "BitSpan subspan offset out of bounds");
    PJ_ASSERT(offset_bits + count_bits <= bit_length, "BitSpan subspan range out of bounds");
    return BitSpan{bytes, bit_offset + offset_bits, count_bits};
  }
};

}  // namespace pj
