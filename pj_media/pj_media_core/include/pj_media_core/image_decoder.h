#pragma once

#include <cstddef>
#include <cstdint>

#include "pj_base/expected.hpp"
#include "pj_media_core/cancel_token.h"
#include "pj_media_core/decoded_frame.h"

namespace PJ {

class ImageDecoder {
 public:
  ImageDecoder();
  ~ImageDecoder();

  ImageDecoder(const ImageDecoder&) = delete;
  ImageDecoder& operator=(const ImageDecoder&) = delete;
  ImageDecoder(ImageDecoder&&) = delete;
  ImageDecoder& operator=(ImageDecoder&&) = delete;

  Expected<DecodedFrame> decodeJpeg(const uint8_t* data, size_t size, const CancelTokenPtr& cancel = nullptr) const;

  static Expected<DecodedFrame> decodeRaw(const uint8_t* data, size_t size, int width, int height, PixelFormat format);

 private:
  void* tj_handle_ = nullptr;
};

}  // namespace PJ
