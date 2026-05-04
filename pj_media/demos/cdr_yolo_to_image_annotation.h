#pragma once

#include <cstddef>
#include <cstdint>

#include "pj_base/expected.hpp"
#include "pj_marker_protocol/image_annotation.h"

namespace pj_demos {

/// Decodes a CDR-encoded `yolo_msgs/msg/DetectionArray` (yolo_ros) into a
/// canonical `PJ::ImageAnnotation`. Each detection's BoundingBox2D becomes a
/// 4-point LineLoop colored by integer `class_id` (palette indexed). Mask and
/// keypoint payloads are decoded-and-discarded — the demo doesn't render them.
///
/// Wire layout (CDR, after the 4-byte CDR header):
///   header                std_msgs/Header
///   detections            Detection[]
///
/// Detection (relevant fields, in order):
///   class_id              int32
///   class_name            string
///   score                 double
///   id                    string
///   bbox                  BoundingBox2D  (Pose2D + Vector2)
///   bbox3d                BoundingBox3D  (decoded-and-discarded, 10 doubles + frame_id)
///   mask                  Mask           (decoded-and-discarded)
///   keypoints             KeyPoint2DArray (decoded-and-discarded)
///   keypoints3d           KeyPoint3DArray (decoded-and-discarded)
[[nodiscard]] PJ::Expected<PJ::ImageAnnotation> cdrYoloDetectionArrayToImageAnnotation(
    const uint8_t* data, size_t size);

}  // namespace pj_demos
