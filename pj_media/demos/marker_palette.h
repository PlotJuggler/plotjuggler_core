#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "pj_marker_protocol/image_annotation.h"

// Loader-side helpers for producing canonical ImageAnnotation structs from
// CDR-encoded ROS 2 marker messages. These are demo-local — every loader/plugin
// gets to choose its own palette and label policy. Two demos that happen to
// share a source format can share these helpers; otherwise duplication is fine
// (and DRY only kicks in when it's actually needed).
namespace pj_demos {

/// Stable color per integer class id. Vivid hues with sufficient contrast to
/// tell neighbouring classes apart on top of arbitrary camera frames. Reused
/// across frames so the same class keeps the same color throughout playback.
[[nodiscard]] PJ::ColorRGBA colorForClass(int32_t class_id);

/// FNV-1a 32-bit hash of a string. Maps a textual class identifier (vision_msgs
/// uses strings) to a stable palette index, so the same input always produces
/// the same color across frames even if the publisher reorders detections.
[[nodiscard]] uint32_t fnv1a32(std::string_view s);

/// Compose a "label score" string with score truncated to 2 decimals. Empty
/// label returns an empty string. NaN/inf scores fall back to just the label.
[[nodiscard]] std::string formatLabel(const std::string& label, double score);

/// Build a TextAnnotation positioned just above the top edge of a bbox whose
/// center is `(cx, cy)` and whose half-extents are `(hx, hy)`. Caller is
/// responsible for skipping empty-label cases.
[[nodiscard]] PJ::TextAnnotation makeBboxLabel(const std::string& label_text, double cx, double cy,
                                                double hx, double hy, const PJ::ColorRGBA& color);

/// Build a 4-corner LineLoop PointsAnnotation from a centre+size bbox. Corners
/// are emitted clockwise starting top-left; thickness is 2 px to match the
/// demo's visual style.
[[nodiscard]] PJ::PointsAnnotation makeBboxLineLoop(double cx, double cy, double sx, double sy,
                                                     const PJ::ColorRGBA& color);

}  // namespace pj_demos
