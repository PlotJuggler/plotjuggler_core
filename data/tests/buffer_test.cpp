#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "pj/engine/buffer.hpp"

namespace pj::engine {
namespace {

// ===========================================================================
// RawBuffer tests
// ===========================================================================

TEST(RawBufferTest, DefaultConstructEmpty) {
  RawBuffer buf;
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(RawBufferTest, ConstructWithCapacity) {
  constexpr std::size_t kCap = 256;
  RawBuffer buf(kCap);
  EXPECT_TRUE(buf.empty());
  EXPECT_GE(buf.capacity(), kCap);
}

TEST(RawBufferTest, AppendData) {
  RawBuffer buf;
  const std::array<uint8_t, 4> payload = {0xDE, 0xAD, 0xBE, 0xEF};
  buf.append(payload.data(), payload.size());

  EXPECT_EQ(buf.size(), 4u);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf.data()[0], 0xDE);
  EXPECT_EQ(buf.data()[1], 0xAD);
  EXPECT_EQ(buf.data()[2], 0xBE);
  EXPECT_EQ(buf.data()[3], 0xEF);
}

TEST(RawBufferTest, AppendMultipleTimes) {
  RawBuffer buf;
  const std::array<uint8_t, 3> first = {1, 2, 3};
  const std::array<uint8_t, 5> second = {4, 5, 6, 7, 8};

  buf.append(first.data(), first.size());
  buf.append(second.data(), second.size());

  EXPECT_EQ(buf.size(), 8u);
  for (uint8_t i = 0; i < 8; ++i) {
    EXPECT_EQ(buf.data()[i], i + 1);
  }
}

TEST(RawBufferTest, Resize) {
  RawBuffer buf;
  buf.resize(16);
  EXPECT_EQ(buf.size(), 16u);
  EXPECT_FALSE(buf.empty());
}

TEST(RawBufferTest, Clear) {
  RawBuffer buf;
  const std::array<uint8_t, 4> payload = {1, 2, 3, 4};
  buf.append(payload.data(), payload.size());
  EXPECT_FALSE(buf.empty());

  buf.clear();
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(RawBufferTest, Reserve) {
  RawBuffer buf;
  buf.reserve(1000);
  EXPECT_GE(buf.capacity(), 1000u);
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_TRUE(buf.empty());
}

// ===========================================================================
// Validity bitmap tests
// ===========================================================================

TEST(ValidityBitmapTest, BytesForBits) {
  EXPECT_EQ(validity_bitmap::bytes_for_bits(0), 0u);
  EXPECT_EQ(validity_bitmap::bytes_for_bits(1), 1u);
  EXPECT_EQ(validity_bitmap::bytes_for_bits(7), 1u);
  EXPECT_EQ(validity_bitmap::bytes_for_bits(8), 1u);
  EXPECT_EQ(validity_bitmap::bytes_for_bits(9), 2u);
  EXPECT_EQ(validity_bitmap::bytes_for_bits(16), 2u);
  EXPECT_EQ(validity_bitmap::bytes_for_bits(17), 3u);
}

TEST(ValidityBitmapTest, InitAllValid) {
  RawBuffer buf;
  validity_bitmap::init(buf, 16);

  EXPECT_EQ(buf.size(), 2u);
  for (std::size_t i = 0; i < 16; ++i) {
    EXPECT_TRUE(validity_bitmap::is_valid(buf, i))
        << "bit " << i << " should be valid after init";
  }
}

TEST(ValidityBitmapTest, SetNull) {
  RawBuffer buf;
  validity_bitmap::init(buf, 16);

  validity_bitmap::set_null(buf, 5);
  EXPECT_FALSE(validity_bitmap::is_valid(buf, 5));

  // All other bits should remain valid.
  for (std::size_t i = 0; i < 16; ++i) {
    if (i == 5) continue;
    EXPECT_TRUE(validity_bitmap::is_valid(buf, i))
        << "bit " << i << " should still be valid";
  }
}

TEST(ValidityBitmapTest, SetValidAfterNull) {
  RawBuffer buf;
  validity_bitmap::init(buf, 16);

  validity_bitmap::set_null(buf, 5);
  EXPECT_FALSE(validity_bitmap::is_valid(buf, 5));

  validity_bitmap::set_valid(buf, 5);
  EXPECT_TRUE(validity_bitmap::is_valid(buf, 5));
}

TEST(ValidityBitmapTest, CountNulls) {
  RawBuffer buf;
  validity_bitmap::init(buf, 16);

  validity_bitmap::set_null(buf, 3);
  validity_bitmap::set_null(buf, 7);
  validity_bitmap::set_null(buf, 15);

  EXPECT_EQ(validity_bitmap::count_nulls(buf, 16), 3u);
}

TEST(ValidityBitmapTest, ByteBoundary) {
  RawBuffer buf;
  validity_bitmap::init(buf, 16);

  // Bit 7 is the last bit in byte 0; bit 8 is the first bit in byte 1.
  validity_bitmap::set_null(buf, 7);
  validity_bitmap::set_null(buf, 8);

  EXPECT_FALSE(validity_bitmap::is_valid(buf, 7));
  EXPECT_FALSE(validity_bitmap::is_valid(buf, 8));

  // Neighbours should be unaffected.
  EXPECT_TRUE(validity_bitmap::is_valid(buf, 6));
  EXPECT_TRUE(validity_bitmap::is_valid(buf, 9));
}

TEST(ValidityBitmapTest, CountNullsWithNoNulls) {
  RawBuffer buf;
  validity_bitmap::init(buf, 32);
  EXPECT_EQ(validity_bitmap::count_nulls(buf, 32), 0u);
}

}  // namespace
}  // namespace pj::engine
