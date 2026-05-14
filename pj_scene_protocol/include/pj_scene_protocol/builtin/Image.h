/**
 * @file Image.h
 * @brief Image built-in object: raw or compressed, identified by an
 *        encoding string.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Image. The `encoding` string distinguishes raw pixel layouts from
/// compressed wire formats; the producer decides which.
///
///   - Raw encodings: "rgb8", "rgba8", "bgr8", "bgra8", "mono8", "mono16".
///     `data` is `row_step * height` bytes laid out per the encoding.
///     `row_step` may exceed `width * bytes_per_pixel(encoding)` when the
///     wire format includes per-row padding; consumers must honor it.
///     `is_bigendian` is meaningful only for mono16 (and any future
///     multi-byte raw encoding).
///
///   - Compressed encodings: "jpeg", "png", "qoi". `data` carries the
///     compressed payload; consumers run the appropriate codec to obtain
///     decoded pixels. `row_step` and `is_bigendian` are unused.
///
///   - Compressed depth: "compressedDepth" (ROS-style). `data` carries a
///     PNG payload that decodes to grayscale; `compressed_depth_min` and
///     `compressed_depth_max` carry the quantization range needed to map
///     the grayscale back to depth values.
///
/// See pj_scene_protocol/builtin/CommonImageEncoding.h for the documented
/// vocabulary of canonical encoding strings, with helpers to parse and
/// emit them.
///
/// `anchor` keeps the underlying buffer alive — the producer may have made
/// `data` a view into the source payload (zero-copy) or into a freshly
/// allocated vector (when the wire format required conversion); consumers
/// don't need to know which.
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::string encoding;       ///< raw or compressed; see vocabulary above.
  uint32_t row_step = 0;      ///< raw encodings only; 0 for compressed.
  bool is_bigendian = false;  ///< mono16 only.
  Span<const uint8_t> data;
  BufferAnchor anchor;

  /// ROS compressedDepth metadata: depth-quantization range used after
  /// PNG decoding to map grayscale back to depth values. Both nullopt for
  /// regular images.
  std::optional<float> compressed_depth_min;
  std::optional<float> compressed_depth_max;

  Timestamp timestamp_ns = 0;
};

}  // namespace sdk
}  // namespace PJ
