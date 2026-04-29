#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nanocdr/nanocdr.hpp"
#include "pj_media_core/scene_decoder.h"

namespace PJ {
namespace {

// Stable color per class_id. Vivid hues with sufficient contrast to tell
// neighbouring classes apart on top of arbitrary camera frames. Reused across
// frames so the same class keeps the same color throughout playback.
constexpr ColorRGBA kClassPalette[] = {
    {  0, 255,   0, 255},  // green
    {255,  64,  64, 255},  // red
    { 64, 128, 255, 255},  // blue
    {255, 192,   0, 255},  // amber
    {255,   0, 255, 255},  // magenta
    {  0, 255, 255, 255},  // cyan
    {255, 128,   0, 255},  // orange
    {128, 255,   0, 255},  // lime
    {200, 100, 255, 255},  // violet
    {255, 200, 200, 255},  // pink
};
constexpr ColorRGBA colorForClass(int32_t class_id) {
  // Negative ids fold into the palette via unsigned wrap.
  auto idx = static_cast<uint32_t>(class_id) % (sizeof(kClassPalette) / sizeof(kClassPalette[0]));
  return kClassPalette[idx];
}

// FNV-1a 32-bit hash of a string. Used to map textual class_id (vision_msgs uses
// string class identifiers) to a deterministic palette index. Same input always
// hashes to the same colour across frames.
uint32_t fnv1a32(const std::string& s) {
  uint32_t h = 0x811c9dc5u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 0x01000193u;
  }
  return h;
}

// Compose "label score" with score truncated to 2 decimals. Empty label → empty
// string (caller should skip emission). NaN/inf scores produce just the label.
std::string formatLabel(const std::string& label, double score) {
  if (label.empty()) return {};
  char buf[64] = {0};
  if (score == score && score > -1e9 && score < 1e9) {
    std::snprintf(buf, sizeof(buf), "%s %.2f", label.c_str(), score);
  } else {
    std::snprintf(buf, sizeof(buf), "%s", label.c_str());
  }
  return std::string(buf);
}

// Build a TextAnnotation positioned just above the top edge of a bbox whose
// center is (cx, cy) and whose half-extents are (hx, hy). Returns std::nullopt
// when the label is empty so callers can skip the push.
TextAnnotation makeBboxLabel(const std::string& label_text, double cx, double cy, double hx, double hy,
                             const ColorRGBA& color) {
  TextAnnotation t;
  t.text = label_text;
  t.position = {cx - hx, cy - hy - 18.0};
  t.font_size = 14.0;
  t.color = color;
  return t;
}

// CDR decoder for ROS 2 vision_msgs/msg/Detection2DArray.
//
// Wire layout (CDR, after the 4-byte CDR header):
//   header.stamp.sec       uint32
//   header.stamp.nanosec   uint32
//   header.frame_id        string
//   detections             Detection2D[]
//
// Detection2D:
//   header                 std_msgs/Header
//   results                ObjectHypothesisWithPose[]
//   bbox                   BoundingBox2D
//   id                     string
//
// We discard everything except the bboxes — each bounding box becomes a
// 4-corner LineLoop in the resulting SceneFrame.
class CdrDetection2DArrayDecoder final : public ISceneDecoder {
 public:
  Expected<SceneFrame> decode(const uint8_t* data, size_t size) override {
    if (data == nullptr || size < 4) {
      return unexpected(std::string("CDR Detection2DArray: buffer too small"));
    }
    try {
      nanocdr::Decoder dec(nanocdr::ConstBuffer(data, size));

      // Outer std_msgs/Header
      uint32_t sec = 0;
      uint32_t nanosec = 0;
      dec.decode(sec);
      dec.decode(nanosec);
      std::string outer_frame_id;
      dec.decode(outer_frame_id);
      const Timestamp top_ts = static_cast<int64_t>(sec) * 1'000'000'000LL + static_cast<int64_t>(nanosec);

      // detections[]
      std::vector<PointsAnnotation> bboxes;
      std::vector<TextAnnotation> labels;
      uint32_t n_detections = 0;
      dec.decode(n_detections);
      bboxes.reserve(n_detections);
      labels.reserve(n_detections);

      for (uint32_t i = 0; i < n_detections; ++i) {
        // per-detection Header
        uint32_t det_sec = 0;
        uint32_t det_nanosec = 0;
        dec.decode(det_sec);
        dec.decode(det_nanosec);
        std::string det_frame_id;
        dec.decode(det_frame_id);

        // results[] : ObjectHypothesisWithPose = {class_id (string), score (double),
        //                                          PoseWithCovariance (7 + 36 doubles)}.
        // Capture the FIRST hypothesis's class_id and score for the label.
        uint32_t n_results = 0;
        dec.decode(n_results);
        std::string first_class_id;
        double first_score = std::nan("");
        for (uint32_t j = 0; j < n_results; ++j) {
          std::string class_id;
          dec.decode(class_id);
          double score = 0;
          dec.decode(score);
          if (j == 0) {
            first_class_id = std::move(class_id);
            first_score = score;
          }
          for (int k = 0; k < 7 + 36; ++k) {
            double dummy = 0;
            dec.decode(dummy);
          }
        }

        // BoundingBox2D: center(Pose2D = x, y, theta) + size_x + size_y
        double cx = 0, cy = 0, theta = 0, sx = 0, sy = 0;
        dec.decode(cx);
        dec.decode(cy);
        dec.decode(theta);
        dec.decode(sx);
        dec.decode(sy);

        // id
        std::string id;
        dec.decode(id);

        PointsAnnotation bbox;
        bbox.topology = AnnotationTopology::kLineLoop;
        bbox.points = {
            {cx - sx / 2.0, cy - sy / 2.0},
            {cx + sx / 2.0, cy - sy / 2.0},
            {cx + sx / 2.0, cy + sy / 2.0},
            {cx - sx / 2.0, cy + sy / 2.0},
        };
        // Pick a stable colour per class_id. Hashing the string lets the same
        // class keep the same colour even if the publisher re-orders detections
        // between frames. Fallback to the bbox index when no hypothesis exists.
        bbox.color = first_class_id.empty()
                         ? colorForClass(static_cast<int32_t>(i))
                         : colorForClass(static_cast<int32_t>(fnv1a32(first_class_id)));
        bbox.thickness = 2.0;

        if (!first_class_id.empty()) {
          labels.push_back(makeBboxLabel(formatLabel(first_class_id, first_score), cx, cy, sx / 2.0, sy / 2.0,
                                          bbox.color));
        }
        bboxes.push_back(std::move(bbox));
      }

      ImageAnnotation ia;
      ia.timestamp = top_ts;
      ia.image_topic = outer_frame_id;  // best-effort link; demo wires explicitly
      ia.points = std::move(bboxes);
      ia.texts = std::move(labels);

      SceneFrame sf;
      sf.timestamp = top_ts;
      sf.annotations.push_back(std::move(ia));
      return sf;
    } catch (const std::exception& e) {
      return unexpected(std::string("CDR Detection2DArray decode failed: ") + e.what());
    } catch (...) {
      return unexpected(std::string("CDR Detection2DArray decode failed: unknown error"));
    }
  }
};

// CDR decoder for yolo_msgs/msg/DetectionArray (https://github.com/mgonzs13/yolo_ros).
// Each Detection's BoundingBox2D becomes a 4-point LineLoop colored by class_id.
// Mask/keypoint fields exist in the wire payload but we decode-and-discard them.
class CdrYoloDetectionArrayDecoder final : public ISceneDecoder {
 public:
  Expected<SceneFrame> decode(const uint8_t* data, size_t size) override {
    if (data == nullptr || size < 4) {
      return unexpected(std::string("CDR yolo DetectionArray: buffer too small"));
    }
    try {
      nanocdr::Decoder dec(nanocdr::ConstBuffer(data, size));

      // Top-level std_msgs/Header
      uint32_t sec = 0;
      uint32_t nanosec = 0;
      dec.decode(sec);
      dec.decode(nanosec);
      std::string outer_frame_id;
      dec.decode(outer_frame_id);
      const Timestamp top_ts = static_cast<int64_t>(sec) * 1'000'000'000LL + static_cast<int64_t>(nanosec);

      std::vector<PointsAnnotation> bboxes;
      std::vector<TextAnnotation> labels;
      uint32_t n_detections = 0;
      dec.decode(n_detections);
      bboxes.reserve(n_detections);
      labels.reserve(n_detections);

      for (uint32_t i = 0; i < n_detections; ++i) {
        // Detection fields, in order
        int32_t class_id = 0;
        dec.decode(class_id);
        std::string class_name;
        dec.decode(class_name);
        double score = 0;
        dec.decode(score);
        std::string id;
        dec.decode(id);

        // BoundingBox2D = Pose2D{Point2D{x,y}, theta} + Vector2{x,y}
        double cx = 0, cy = 0, theta = 0, sx = 0, sy = 0;
        dec.decode(cx);
        dec.decode(cy);
        dec.decode(theta);
        dec.decode(sx);
        dec.decode(sy);

        // BoundingBox3D = Pose{Point3, Quaternion} + Vector3 + frame_id
        // 3 + 4 + 3 = 10 doubles, then string
        for (int k = 0; k < 10; ++k) {
          double dummy = 0;
          dec.decode(dummy);
        }
        std::string bbox3d_frame_id;
        dec.decode(bbox3d_frame_id);

        // Mask = int32 height + int32 width + Point2D[] data
        int32_t mask_h = 0, mask_w = 0;
        dec.decode(mask_h);
        dec.decode(mask_w);
        uint32_t n_mask = 0;
        dec.decode(n_mask);
        for (uint32_t j = 0; j < n_mask; ++j) {
          double mx = 0, my = 0;
          dec.decode(mx);
          dec.decode(my);
        }

        // KeyPoint2DArray = KeyPoint2D[] data, where KeyPoint2D = int32 id + Point2D + double score
        uint32_t n_kp2 = 0;
        dec.decode(n_kp2);
        for (uint32_t j = 0; j < n_kp2; ++j) {
          int32_t kp_id = 0;
          dec.decode(kp_id);
          double kx = 0, ky = 0, ks = 0;
          dec.decode(kx);
          dec.decode(ky);
          dec.decode(ks);
        }

        // KeyPoint3DArray = KeyPoint3D[] data + frame_id
        uint32_t n_kp3 = 0;
        dec.decode(n_kp3);
        for (uint32_t j = 0; j < n_kp3; ++j) {
          int32_t kp_id = 0;
          dec.decode(kp_id);
          double px = 0, py = 0, pz = 0, ks = 0;
          dec.decode(px);
          dec.decode(py);
          dec.decode(pz);
          dec.decode(ks);
        }
        std::string kp3_frame_id;
        dec.decode(kp3_frame_id);

        PointsAnnotation bbox;
        bbox.topology = AnnotationTopology::kLineLoop;
        bbox.points = {
            {cx - sx / 2.0, cy - sy / 2.0},
            {cx + sx / 2.0, cy - sy / 2.0},
            {cx + sx / 2.0, cy + sy / 2.0},
            {cx - sx / 2.0, cy + sy / 2.0},
        };
        bbox.color = colorForClass(class_id);
        bbox.thickness = 2.0;

        if (!class_name.empty()) {
          labels.push_back(
              makeBboxLabel(formatLabel(class_name, score), cx, cy, sx / 2.0, sy / 2.0, bbox.color));
        }
        bboxes.push_back(std::move(bbox));
      }

      ImageAnnotation ia;
      ia.timestamp = top_ts;
      ia.image_topic = outer_frame_id;
      ia.points = std::move(bboxes);
      ia.texts = std::move(labels);

      SceneFrame sf;
      sf.timestamp = top_ts;
      sf.annotations.push_back(std::move(ia));
      return sf;
    } catch (const std::exception& e) {
      return unexpected(std::string("CDR yolo DetectionArray decode failed: ") + e.what());
    } catch (...) {
      return unexpected(std::string("CDR yolo DetectionArray decode failed: unknown error"));
    }
  }
};

}  // namespace

std::unique_ptr<ISceneDecoder> makeSceneDecoderCdrDetection2DArray() {
  return std::make_unique<CdrDetection2DArrayDecoder>();
}

std::unique_ptr<ISceneDecoder> makeSceneDecoderCdrYoloDetectionArray() {
  return std::make_unique<CdrYoloDetectionArrayDecoder>();
}

}  // namespace PJ
