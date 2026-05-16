#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace PJ::builtin_wire {

enum class WireType : uint32_t {
  kVarint = 0,
  kFixed64 = 1,
  kLengthDelimited = 2,
  kFixed32 = 5,
};

struct Tag {
  uint32_t field = 0;
  WireType type = WireType::kVarint;
};

class Writer {
 public:
  explicit Writer(std::vector<uint8_t>& out) : out_(out) {}

  void varint(uint32_t field, uint64_t value) {
    tag(field, WireType::kVarint);
    appendVarint(value);
  }

  void fixed64(uint32_t field, uint64_t value) {
    tag(field, WireType::kFixed64);
    appendFixed64(value);
  }

  void doubleField(uint32_t field, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(value));
    fixed64(field, bits);
  }

  void string(uint32_t field, std::string_view value) {
    bytes(field, reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }

  void bytes(uint32_t field, const uint8_t* data, size_t size) {
    tag(field, WireType::kLengthDelimited);
    appendVarint(size);
    if (size != 0) {
      out_.insert(out_.end(), data, data + size);
    }
  }

  template <typename BuildMessage>
  void message(uint32_t field, BuildMessage&& build_message) {
    std::vector<uint8_t> body;
    Writer nested(body);
    build_message(nested);
    bytes(field, body.data(), body.size());
  }

 private:
  void tag(uint32_t field, WireType type) {
    appendVarint((static_cast<uint64_t>(field) << 3) | static_cast<uint32_t>(type));
  }

  void appendVarint(uint64_t value) {
    while (value >= 0x80u) {
      out_.push_back(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
      value >>= 7;
    }
    out_.push_back(static_cast<uint8_t>(value));
  }

  void appendFixed64(uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      out_.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
    }
  }

  std::vector<uint8_t>& out_;
};

class Reader {
 public:
  Reader() = default;
  Reader(const uint8_t* data, size_t size) : p_(data), end_(data == nullptr ? nullptr : data + size) {}

  bool eof() const noexcept {
    return p_ == nullptr || p_ >= end_;
  }

  size_t remaining() const noexcept {
    if (p_ == nullptr) {
      return 0;
    }
    return static_cast<size_t>(end_ - p_);
  }

  bool readTag(Tag& out) {
    uint64_t raw = 0;
    if (!readVarint(raw) || raw == 0) {
      return false;
    }
    out.field = static_cast<uint32_t>(raw >> 3);
    out.type = static_cast<WireType>(raw & 0x7u);
    return out.field != 0;
  }

  bool readVarint(uint64_t& out) {
    out = 0;
    if (p_ == nullptr) {
      return false;
    }
    int shift = 0;
    for (int byte_index = 0; p_ < end_ && byte_index < 10; ++byte_index) {
      const uint8_t byte = *p_++;
      const uint64_t payload = static_cast<uint64_t>(byte & 0x7Fu);
      if (shift == 63 && payload > 1) {
        return false;
      }
      out |= payload << shift;
      if ((byte & 0x80u) == 0) {
        return true;
      }
      shift += 7;
    }
    return false;
  }

  bool readFixed64(uint64_t& out) {
    if (remaining() < 8) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<uint64_t>(p_[i]) << (8 * i);
    }
    p_ += 8;
    return true;
  }

  bool readDouble(double& out) {
    uint64_t bits = 0;
    if (!readFixed64(bits)) {
      return false;
    }
    std::memcpy(&out, &bits, sizeof(out));
    return true;
  }

  bool readString(std::string& out) {
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (!readBytes(data, size)) {
      return false;
    }
    out.assign(reinterpret_cast<const char*>(data), size);
    return true;
  }

  bool readMessage(Reader& out) {
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (!readBytes(data, size)) {
      return false;
    }
    out = Reader(data, size);
    return true;
  }

  bool skip(WireType type) {
    switch (type) {
      case WireType::kVarint: {
        uint64_t ignored = 0;
        return readVarint(ignored);
      }
      case WireType::kFixed64:
        return skipBytes(8);
      case WireType::kLengthDelimited: {
        const uint8_t* ignored = nullptr;
        size_t ignored_size = 0;
        return readBytes(ignored, ignored_size);
      }
      case WireType::kFixed32:
        return skipBytes(4);
      default:
        return false;
    }
  }

 private:
  bool readBytes(const uint8_t*& data, size_t& size) {
    uint64_t len = 0;
    if (!readVarint(len) || len > remaining()) {
      return false;
    }
    data = p_;
    size = static_cast<size_t>(len);
    p_ += size;
    return true;
  }

  bool skipBytes(size_t size) {
    if (remaining() < size) {
      return false;
    }
    p_ += size;
    return true;
  }

  const uint8_t* p_ = nullptr;
  const uint8_t* end_ = nullptr;
};

}  // namespace PJ::builtin_wire
