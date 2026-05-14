/**
 * @file CommonImageEncoding.h
 * @brief Optional vocabulary of canonical image encoding strings.
 *
 * sdk::Image::encoding is an open std::string — producers and consumers
 * are not required to use the values listed here. This header documents
 * the encodings the in-tree producers (parser_ros, future plugins) emit
 * and the consumers (pj_scene2D) recognise, plus a magic_enum-backed
 * round-trip helper.
 *
 * The enumerator NAMES are the canonical strings (so
 * magic_enum::enum_name(rgb8) == "rgb8"). Use parse() to convert a
 * string from the wire into the enum, and name() to produce a string
 * to emit.
 */
#pragma once

#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string_view>

namespace PJ {
namespace sdk {

// NOLINTBEGIN(readability-identifier-naming)
// Enumerator names are deliberately the canonical encoding strings, not
// the project's kCamelCase constants.
enum class CommonImageEncoding : uint8_t {
  // Raw pixel layouts
  rgb8,    ///< 3 bytes/pixel, R-G-B order.
  rgba8,   ///< 4 bytes/pixel, R-G-B-A order.
  bgr8,    ///< 3 bytes/pixel, B-G-R order.
  bgra8,   ///< 4 bytes/pixel, B-G-R-A order.
  mono8,   ///< 1 byte/pixel, grayscale.
  mono16,  ///< 2 bytes/pixel, grayscale.
  // Compressed wire formats
  jpeg,
  png,
  qoi,
  // ROS-specific
  compressedDepth,  ///< PNG payload + depth quantization range in extras.
};
// NOLINTEND(readability-identifier-naming)

/// Canonical string for an encoding value. Same as
/// magic_enum::enum_name(e) but kept as a one-liner for callers that
/// don't want to know about magic_enum.
[[nodiscard]] inline constexpr std::string_view name(CommonImageEncoding e) noexcept {
  return magic_enum::enum_name(e);
}

/// Parse an encoding string into the enum. Returns nullopt if the
/// string isn't one of the documented vocabulary entries.
[[nodiscard]] inline constexpr std::optional<CommonImageEncoding> parseImageEncoding(std::string_view s) noexcept {
  return magic_enum::enum_cast<CommonImageEncoding>(s);
}

}  // namespace sdk
}  // namespace PJ
