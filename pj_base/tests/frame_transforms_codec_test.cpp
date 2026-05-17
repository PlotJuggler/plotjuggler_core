// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_base/builtin/frame_transforms_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace PJ {
namespace {

using sdk::FrameTransform;
using sdk::FrameTransforms;

namespace pb {

inline void appendVarint(std::vector<uint8_t>& out, uint64_t v) {
  while (v >= 0x80u) {
    out.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
    v >>= 7;
  }
  out.push_back(static_cast<uint8_t>(v));
}

inline void appendTag(std::vector<uint8_t>& out, uint32_t field, uint32_t wire) {
  appendVarint(out, (static_cast<uint64_t>(field) << 3) | wire);
}

inline void appendDouble(std::vector<uint8_t>& out, double v) {
  uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof(v));
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu));
  }
}

inline void appendLenDelim(std::vector<uint8_t>& out, const std::vector<uint8_t>& body) {
  appendVarint(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
}

inline void appendString(std::vector<uint8_t>& out, const std::string& value) {
  appendVarint(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
}

}  // namespace pb

std::vector<uint8_t> encodeTimestamp(Timestamp timestamp_ns) {
  constexpr int64_t ns_per_second = 1000LL * 1000LL * 1000LL;
  int64_t seconds = timestamp_ns / ns_per_second;
  int32_t nanos = static_cast<int32_t>(timestamp_ns % ns_per_second);
  if (nanos < 0) {
    --seconds;
    nanos += static_cast<int32_t>(ns_per_second);
  }

  std::vector<uint8_t> body;
  pb::appendTag(body, 1, 0);
  pb::appendVarint(body, static_cast<uint64_t>(seconds));
  pb::appendTag(body, 2, 0);
  pb::appendVarint(body, static_cast<uint32_t>(nanos));
  return body;
}

std::vector<uint8_t> encodeVector3(double x, double y, double z) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 1, 1);
  pb::appendDouble(body, x);
  pb::appendTag(body, 2, 1);
  pb::appendDouble(body, y);
  pb::appendTag(body, 3, 1);
  pb::appendDouble(body, z);
  return body;
}

std::vector<uint8_t> encodeQuaternion(double x, double y, double z, double w) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 1, 1);
  pb::appendDouble(body, x);
  pb::appendTag(body, 2, 1);
  pb::appendDouble(body, y);
  pb::appendTag(body, 3, 1);
  pb::appendDouble(body, z);
  pb::appendTag(body, 4, 1);
  pb::appendDouble(body, w);
  return body;
}

std::vector<uint8_t> encodeFrameTransform(const FrameTransform& transform) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 1, 2);
  pb::appendLenDelim(body, encodeTimestamp(transform.timestamp));
  pb::appendTag(body, 2, 2);
  pb::appendString(body, transform.parent_frame_id);
  pb::appendTag(body, 3, 2);
  pb::appendString(body, transform.child_frame_id);
  pb::appendTag(body, 4, 2);
  pb::appendLenDelim(body, encodeVector3(transform.translation.x, transform.translation.y, transform.translation.z));
  pb::appendTag(body, 5, 2);
  pb::appendLenDelim(
      body, encodeQuaternion(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w));
  return body;
}

TEST(FrameTransformsCodecTest, SchemaNameMatchesFrameTransforms) {
  EXPECT_EQ(kSchemaFrameTransforms, "PJ.FrameTransforms");
}

TEST(FrameTransformsCodecTest, EmptyMessageProducesEmptyBytes) {
  FrameTransforms transforms;
  EXPECT_TRUE(serializeFrameTransforms(transforms).empty());
}

TEST(FrameTransformsCodecTest, GoldenBytesSingleTransform) {
  FrameTransforms transforms;
  transforms.transforms.push_back(
      FrameTransform{
          .timestamp = 1'234'567'890'123,
          .parent_frame_id = "map",
          .child_frame_id = "base_link",
          .translation = {.x = 1.0, .y = 2.0, .z = 3.0},
          .rotation = {.x = 0.0, .y = 0.0, .z = 0.707, .w = 0.707},
      });

  std::vector<uint8_t> expected;
  pb::appendTag(expected, 1, 2);
  pb::appendLenDelim(expected, encodeFrameTransform(transforms.transforms.front()));

  EXPECT_EQ(serializeFrameTransforms(transforms), expected);
}

TEST(FrameTransformsCodecTest, RoundTripMultipleTransforms) {
  FrameTransforms input;
  input.transforms.push_back(
      FrameTransform{
          .timestamp = 42,
          .parent_frame_id = "map",
          .child_frame_id = "odom",
          .translation = {.x = 1.0, .y = 0.0, .z = 0.0},
          .rotation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0},
      });
  input.transforms.push_back(
      FrameTransform{
          .timestamp = -1,
          .parent_frame_id = "odom",
          .child_frame_id = "base_link",
          .translation = {.x = -1.5, .y = 2.5, .z = 3.5},
          .rotation = {.x = 0.1, .y = 0.2, .z = 0.3, .w = 0.9},
      });

  const auto bytes = serializeFrameTransforms(input);
  auto output = deserializeFrameTransforms(bytes.data(), bytes.size());

  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(*output, input);
}

TEST(FrameTransformsCodecTest, EmptyBufferProducesError) {
  const std::vector<uint8_t> bytes;
  auto output = deserializeFrameTransforms(bytes.data(), bytes.size());
  EXPECT_FALSE(output.has_value());
}

TEST(FrameTransformsCodecTest, InvalidNestedMessageProducesError) {
  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 1, 2);
  pb::appendVarint(bytes, 10);
  bytes.push_back(0x08);

  auto output = deserializeFrameTransforms(bytes.data(), bytes.size());
  EXPECT_FALSE(output.has_value());
}

}  // namespace
}  // namespace PJ
