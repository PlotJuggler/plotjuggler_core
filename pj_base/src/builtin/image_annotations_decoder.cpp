#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "pj_base/builtin/image_annotations_codec.h"
#include "protobuf_wire.h"

namespace PJ {
namespace {

using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using sdk::AnnotationTopology;
using sdk::CircleAnnotation;
using sdk::ColorRGBA;
using sdk::Point2;
using sdk::PointsAnnotation;
using sdk::TextAnnotation;

AnnotationTopology mapTopology(uint64_t type) {
  switch (type) {
    case 1:
      return AnnotationTopology::kPoints;
    case 2:
      return AnnotationTopology::kLineLoop;
    case 3:
      return AnnotationTopology::kLineStrip;
    case 4:
      return AnnotationTopology::kLineList;
    case 0:
    default:
      return AnnotationTopology::kPoints;
  }
}

uint8_t normalizedToByte(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return static_cast<uint8_t>(value * 255.0 + 0.5);
}

bool decodePoint2(Reader& reader, Point2& out) {
  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    if (tag.field == 1 && tag.type == WireType::kFixed64) {
      if (!reader.readDouble(out.x)) {
        return false;
      }
    } else if (tag.field == 2 && tag.type == WireType::kFixed64) {
      if (!reader.readDouble(out.y)) {
        return false;
      }
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool decodeColor(Reader& reader, ColorRGBA& out) {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  double a = 1.0;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    if (tag.type == WireType::kFixed64 && tag.field >= 1 && tag.field <= 4) {
      double value = 0.0;
      if (!reader.readDouble(value)) {
        return false;
      }
      switch (tag.field) {
        case 1:
          r = value;
          break;
        case 2:
          g = value;
          break;
        case 3:
          b = value;
          break;
        case 4:
          a = value;
          break;
        default:
          break;
      }
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }

  out = {normalizedToByte(r), normalizedToByte(g), normalizedToByte(b), normalizedToByte(a)};
  return true;
}

bool readPoint2Message(Reader& reader, Point2& out) {
  Reader nested;
  return reader.readMessage(nested) && decodePoint2(nested, out);
}

bool readColorMessage(Reader& reader, ColorRGBA& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeColor(nested, out);
}

bool decodePointsAnnotation(Reader& reader, PointsAnnotation& out) {
  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    switch (tag.field) {
      case 2: {
        if (tag.type != WireType::kVarint) {
          break;
        }
        uint64_t value = 0;
        if (!reader.readVarint(value)) {
          return false;
        }
        out.topology = mapTopology(value);
        continue;
      }
      case 3: {
        if (tag.type != WireType::kLengthDelimited) {
          break;
        }
        Point2 point;
        if (!readPoint2Message(reader, point)) {
          return false;
        }
        out.points.push_back(point);
        continue;
      }
      case 4:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.color)) {
            return false;
          }
          continue;
        }
        break;
      case 5: {
        if (tag.type != WireType::kLengthDelimited) {
          break;
        }
        ColorRGBA color;
        if (!readColorMessage(reader, color)) {
          return false;
        }
        out.colors.push_back(color);
        continue;
      }
      case 6:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.fill_color)) {
            return false;
          }
          continue;
        }
        break;
      case 7:
        if (tag.type == WireType::kFixed64) {
          if (!reader.readDouble(out.thickness)) {
            return false;
          }
          continue;
        }
        break;
      default:
        break;
    }

    if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool decodeCircleAnnotation(Reader& reader, CircleAnnotation& out) {
  out.color = {0, 255, 0, 255};
  out.fill_color = {0, 0, 0, 0};
  out.thickness = 2.0;
  out.radius = 1.0;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    switch (tag.field) {
      case 2:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readPoint2Message(reader, out.center)) {
            return false;
          }
          continue;
        }
        break;
      case 3: {
        if (tag.type != WireType::kFixed64) {
          break;
        }
        double diameter = 0.0;
        if (!reader.readDouble(diameter)) {
          return false;
        }
        out.radius = diameter * 0.5;
        continue;
      }
      case 4:
        if (tag.type == WireType::kFixed64) {
          if (!reader.readDouble(out.thickness)) {
            return false;
          }
          continue;
        }
        break;
      case 5:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.fill_color)) {
            return false;
          }
          continue;
        }
        break;
      case 6:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.color)) {
            return false;
          }
          continue;
        }
        break;
      default:
        break;
    }

    if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool decodeTextAnnotation(Reader& reader, TextAnnotation& out) {
  out.color = {255, 255, 255, 255};
  out.font_size = 14.0;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    switch (tag.field) {
      case 2:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readPoint2Message(reader, out.position)) {
            return false;
          }
          continue;
        }
        break;
      case 3:
        if (tag.type == WireType::kLengthDelimited) {
          if (!reader.readString(out.text)) {
            return false;
          }
          continue;
        }
        break;
      case 4:
        if (tag.type == WireType::kFixed64) {
          if (!reader.readDouble(out.font_size)) {
            return false;
          }
          continue;
        }
        break;
      case 5:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.color)) {
            return false;
          }
          continue;
        }
        break;
      default:
        break;
    }

    if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

}  // namespace

Expected<sdk::ImageAnnotations> deserializeImageAnnotations(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("ImageAnnotations wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::ImageAnnotations annotations;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return unexpected(std::string("ImageAnnotations wire: bad tag"));
    }

    if (tag.type != WireType::kLengthDelimited) {
      if (!reader.skip(tag.type)) {
        return unexpected(std::string("ImageAnnotations wire: skip failed"));
      }
      continue;
    }

    Reader nested;
    if (!reader.readMessage(nested)) {
      return unexpected(std::string("ImageAnnotations wire: bad nested message length"));
    }

    switch (tag.field) {
      case 1: {
        CircleAnnotation circle;
        if (!decodeCircleAnnotation(nested, circle)) {
          return unexpected(std::string("ImageAnnotations wire: CircleAnnotation decode failed"));
        }
        annotations.circles.push_back(std::move(circle));
        break;
      }
      case 2: {
        PointsAnnotation points;
        points.color = {0, 255, 0, 255};
        points.thickness = 2.0;
        if (!decodePointsAnnotation(nested, points)) {
          return unexpected(std::string("ImageAnnotations wire: PointsAnnotation decode failed"));
        }
        annotations.points.push_back(std::move(points));
        break;
      }
      case 3: {
        TextAnnotation text;
        if (!decodeTextAnnotation(nested, text)) {
          return unexpected(std::string("ImageAnnotations wire: TextAnnotation decode failed"));
        }
        annotations.texts.push_back(std::move(text));
        break;
      }
      default:
        break;
    }
  }

  return annotations;
}

}  // namespace PJ
