// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_base/builtin/frame_transforms_codec.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "protobuf_wire.hpp"

namespace PJ {
namespace {

using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::FrameTransform;
using sdk::FrameTransforms;
using sdk::Quaternion;
using sdk::Vector3;

constexpr int64_t kNanosecondsPerSecond = 1000LL * 1000LL * 1000LL;

struct TimestampParts {
  int64_t seconds = 0;
  int32_t nanos = 0;
};

TimestampParts splitTimestamp(Timestamp timestamp_ns) {
  TimestampParts out;
  out.seconds = timestamp_ns / kNanosecondsPerSecond;
  out.nanos = static_cast<int32_t>(timestamp_ns % kNanosecondsPerSecond);
  if (out.nanos < 0) {
    --out.seconds;
    out.nanos += static_cast<int32_t>(kNanosecondsPerSecond);
  }
  return out;
}

bool combineTimestamp(const TimestampParts& parts, Timestamp& out) {
  if (parts.nanos < 0 || parts.nanos >= kNanosecondsPerSecond) {
    return false;
  }
  if (parts.seconds > std::numeric_limits<Timestamp>::max() / kNanosecondsPerSecond ||
      parts.seconds < std::numeric_limits<Timestamp>::min() / kNanosecondsPerSecond) {
    return false;
  }
  const Timestamp seconds_ns = parts.seconds * kNanosecondsPerSecond;
  if (seconds_ns > std::numeric_limits<Timestamp>::max() - parts.nanos) {
    return false;
  }
  out = seconds_ns + parts.nanos;
  return true;
}

void writeTimestamp(Writer& writer, Timestamp timestamp_ns) {
  const auto parts = splitTimestamp(timestamp_ns);
  writer.varint(1, static_cast<uint64_t>(parts.seconds));
  writer.varint(2, static_cast<uint32_t>(parts.nanos));
}

void writeVector3(Writer& writer, const Vector3& vector) {
  writer.doubleField(1, vector.x);
  writer.doubleField(2, vector.y);
  writer.doubleField(3, vector.z);
}

void writeQuaternion(Writer& writer, const Quaternion& quaternion) {
  writer.doubleField(1, quaternion.x);
  writer.doubleField(2, quaternion.y);
  writer.doubleField(3, quaternion.z);
  writer.doubleField(4, quaternion.w);
}

void writeFrameTransform(Writer& writer, const FrameTransform& transform) {
  writer.message(1, [&](Writer& nested) { writeTimestamp(nested, transform.timestamp); });
  writer.string(2, transform.parent_frame_id);
  writer.string(3, transform.child_frame_id);
  writer.message(4, [&](Writer& nested) { writeVector3(nested, transform.translation); });
  writer.message(5, [&](Writer& nested) { writeQuaternion(nested, transform.rotation); });
}

bool decodeTimestamp(Reader& reader, Timestamp& out) {
  TimestampParts parts;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    if (tag.field == 1 && tag.type == WireType::kVarint) {
      uint64_t value = 0;
      if (!reader.readVarint(value)) {
        return false;
      }
      parts.seconds = static_cast<int64_t>(value);
    } else if (tag.field == 2 && tag.type == WireType::kVarint) {
      uint64_t value = 0;
      if (!reader.readVarint(value)) {
        return false;
      }
      parts.nanos = static_cast<int32_t>(value);
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }

  return combineTimestamp(parts, out);
}

bool decodeVector3(Reader& reader, Vector3& out) {
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
    } else if (tag.field == 3 && tag.type == WireType::kFixed64) {
      if (!reader.readDouble(out.z)) {
        return false;
      }
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool decodeQuaternion(Reader& reader, Quaternion& out) {
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
    } else if (tag.field == 3 && tag.type == WireType::kFixed64) {
      if (!reader.readDouble(out.z)) {
        return false;
      }
    } else if (tag.field == 4 && tag.type == WireType::kFixed64) {
      if (!reader.readDouble(out.w)) {
        return false;
      }
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool readTimestampMessage(Reader& reader, Timestamp& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeTimestamp(nested, out);
}

bool readVector3Message(Reader& reader, Vector3& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeVector3(nested, out);
}

bool readQuaternionMessage(Reader& reader, Quaternion& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeQuaternion(nested, out);
}

bool decodeFrameTransform(Reader& reader, FrameTransform& out) {
  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    switch (tag.field) {
      case 1:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readTimestampMessage(reader, out.timestamp)) {
            return false;
          }
          continue;
        }
        break;
      case 2:
        if (tag.type == WireType::kLengthDelimited) {
          if (!reader.readString(out.parent_frame_id)) {
            return false;
          }
          continue;
        }
        break;
      case 3:
        if (tag.type == WireType::kLengthDelimited) {
          if (!reader.readString(out.child_frame_id)) {
            return false;
          }
          continue;
        }
        break;
      case 4:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readVector3Message(reader, out.translation)) {
            return false;
          }
          continue;
        }
        break;
      case 5:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readQuaternionMessage(reader, out.rotation)) {
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

std::vector<uint8_t> serializeFrameTransforms(const FrameTransforms& transforms) {
  std::vector<uint8_t> out;
  Writer writer(out);

  for (const auto& transform : transforms.transforms) {
    writer.message(1, [&](Writer& nested) { writeFrameTransform(nested, transform); });
  }

  return out;
}

Expected<sdk::FrameTransforms> deserializeFrameTransforms(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("FrameTransforms wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::FrameTransforms transforms;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return unexpected(std::string("FrameTransforms wire: bad tag"));
    }

    if (tag.type != WireType::kLengthDelimited) {
      if (!reader.skip(tag.type)) {
        return unexpected(std::string("FrameTransforms wire: skip failed"));
      }
      continue;
    }

    Reader nested;
    if (!reader.readMessage(nested)) {
      return unexpected(std::string("FrameTransforms wire: bad nested message length"));
    }

    if (tag.field == 1) {
      FrameTransform transform;
      if (!decodeFrameTransform(nested, transform)) {
        return unexpected(std::string("FrameTransforms wire: FrameTransform decode failed"));
      }
      transforms.transforms.push_back(std::move(transform));
    }
  }

  return transforms;
}

}  // namespace PJ
