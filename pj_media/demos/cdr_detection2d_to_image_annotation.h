#pragma once

#include <cstddef>
#include <cstdint>

#include "pj_base/expected.hpp"
#include "pj_marker_protocol/image_annotation.h"

namespace pj_demos {

/// Decodes a CDR-encoded `vision_msgs/msg/Detection2DArray` into a canonical
/// `PJ::ImageAnnotation`. Each detection becomes a 4-point LineLoop coloured
/// by FNV-1a hash of the first hypothesis's class_id, plus a `"<class_id>
/// <score>"` label above the bbox.
///
/// Wire layout (CDR, after the 4-byte CDR header):
///   header.stamp.sec       uint32
///   header.stamp.nanosec   uint32
///   header.frame_id        string
///   detections             Detection2D[]
///
/// Detection2D:
///   header                 std_msgs/Header
///   results                ObjectHypothesisWithPose[]
///   bbox                   BoundingBox2D
///   id                     string
[[nodiscard]] PJ::Expected<PJ::ImageAnnotation> cdrDetection2DArrayToImageAnnotation(
    const uint8_t* data, size_t size);

}  // namespace pj_demos
