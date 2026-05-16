#include "pj_base/builtin/image_annotations_codec.h"

#include <cstdint>
#include <vector>

#include "protobuf_wire.h"

namespace PJ {
namespace {

using builtin_wire::Writer;
using sdk::AnnotationTopology;
using sdk::CircleAnnotation;
using sdk::ColorRGBA;
using sdk::ImageAnnotations;
using sdk::Point2;
using sdk::PointsAnnotation;
using sdk::TextAnnotation;

void writePoint2(Writer& writer, const Point2& point) {
  writer.doubleField(1, point.x);
  writer.doubleField(2, point.y);
}

void writeColor(Writer& writer, const ColorRGBA& color) {
  writer.doubleField(1, static_cast<double>(color.r) / 255.0);
  writer.doubleField(2, static_cast<double>(color.g) / 255.0);
  writer.doubleField(3, static_cast<double>(color.b) / 255.0);
  writer.doubleField(4, static_cast<double>(color.a) / 255.0);
}

uint32_t topologyToEnum(AnnotationTopology topology) {
  switch (topology) {
    case AnnotationTopology::kPoints:
      return 1;
    case AnnotationTopology::kLineLoop:
      return 2;
    case AnnotationTopology::kLineStrip:
      return 3;
    case AnnotationTopology::kLineList:
      return 4;
  }
  return 1;
}

void writePointsAnnotation(Writer& writer, const PointsAnnotation& points) {
  writer.varint(2, topologyToEnum(points.topology));

  for (const auto& point : points.points) {
    writer.message(3, [&](Writer& nested) { writePoint2(nested, point); });
  }

  writer.message(4, [&](Writer& nested) { writeColor(nested, points.color); });

  for (const auto& color : points.colors) {
    writer.message(5, [&](Writer& nested) { writeColor(nested, color); });
  }

  writer.message(6, [&](Writer& nested) { writeColor(nested, points.fill_color); });
  writer.doubleField(7, points.thickness);
}

void writeCircleAnnotation(Writer& writer, const CircleAnnotation& circle) {
  writer.message(2, [&](Writer& nested) { writePoint2(nested, circle.center); });
  writer.doubleField(3, circle.radius * 2.0);
  writer.doubleField(4, circle.thickness);
  writer.message(5, [&](Writer& nested) { writeColor(nested, circle.fill_color); });
  writer.message(6, [&](Writer& nested) { writeColor(nested, circle.color); });
}

void writeTextAnnotation(Writer& writer, const TextAnnotation& text) {
  writer.message(2, [&](Writer& nested) { writePoint2(nested, text.position); });
  writer.string(3, text.text);
  writer.doubleField(4, text.font_size);
  writer.message(5, [&](Writer& nested) { writeColor(nested, text.color); });
}

}  // namespace

std::vector<uint8_t> serializeImageAnnotations(const ImageAnnotations& annotations) {
  std::vector<uint8_t> out;
  Writer writer(out);

  for (const auto& circle : annotations.circles) {
    writer.message(1, [&](Writer& nested) { writeCircleAnnotation(nested, circle); });
  }
  for (const auto& points : annotations.points) {
    writer.message(2, [&](Writer& nested) { writePointsAnnotation(nested, points); });
  }
  for (const auto& text : annotations.texts) {
    writer.message(3, [&](Writer& nested) { writeTextAnnotation(nested, text); });
  }

  return out;
}

}  // namespace PJ
