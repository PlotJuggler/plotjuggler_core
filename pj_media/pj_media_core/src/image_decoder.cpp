#include "pj_media_core/image_decoder.h"

#include <turbojpeg.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace PJ {

ImageDecoder::ImageDecoder() : tj_handle_(tjInitDecompress()) {}

ImageDecoder::~ImageDecoder() {
  if (tj_handle_ != nullptr) {
    tjDestroy(tj_handle_);
  }
}

Expected<DecodedFrame> ImageDecoder::decodeJpeg(const uint8_t* data, size_t size, const CancelTokenPtr& cancel) const {
  if (tj_handle_ == nullptr) {
    return unexpected("turbojpeg not initialized");
  }
  if (data == nullptr || size == 0) {
    return unexpected("empty input");
  }

  int width = 0;
  int height = 0;
  int subsamp = 0;
  if (tjDecompressHeader2(
          static_cast<tjhandle>(tj_handle_), const_cast<uint8_t*>(data), static_cast<unsigned long>(size), &width,
          &height, &subsamp) != 0) {
    return unexpected(std::string("JPEG header parse failed: ") + tjGetErrorStr());
  }

  if (cancel != nullptr && cancel->isCancelled()) {
    return unexpected("cancelled");
  }

  auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(width * height * 3));
  if (tjDecompress2(
          static_cast<tjhandle>(tj_handle_), const_cast<uint8_t*>(data), static_cast<unsigned long>(size),
          pixels->data(), width, width * 3, height, TJPF_RGB, TJFLAG_FASTUPSAMPLE | TJFLAG_FASTDCT) != 0) {
    return unexpected(std::string("JPEG decode failed: ") + tjGetErrorStr());
  }

  DecodedFrame frame;
  frame.pixels = std::move(pixels);
  frame.width = width;
  frame.height = height;
  frame.format = PixelFormat::kRGB888;
  return frame;
}

Expected<DecodedFrame> ImageDecoder::decodeRaw(
    const uint8_t* data, size_t size, int width, int height, PixelFormat format) {
  if (data == nullptr || size == 0) {
    return unexpected("empty input");
  }

  int channels = 0;
  switch (format) {
    case PixelFormat::kMono8:
      channels = 1;
      break;
    case PixelFormat::kMono16:
      channels = 2;
      break;
    case PixelFormat::kRGB888:
    case PixelFormat::kBGR888:
      channels = 3;
      break;
    case PixelFormat::kRGBA8888:
    case PixelFormat::kBGRA8888:
      channels = 4;
      break;
    default:
      return unexpected("unsupported raw pixel format");
  }

  auto expected_size = static_cast<size_t>(width * height * channels);
  if (size < expected_size) {
    return unexpected("raw buffer too small");
  }

  auto pixels = std::make_shared<std::vector<uint8_t>>(expected_size);
  std::memcpy(pixels->data(), data, expected_size);

  DecodedFrame frame;
  frame.pixels = std::move(pixels);
  frame.width = width;
  frame.height = height;
  frame.format = format;
  return frame;
}

}  // namespace PJ
