#include "pj_media_core/image_decoder.h"

#include <gtest/gtest.h>
#include <turbojpeg.h>

#include <cstdint>
#include <vector>

namespace PJ {
namespace {

std::vector<uint8_t> createTestJpeg(int width, int height) {
  std::vector<uint8_t> rgb(static_cast<size_t>(width * height * 3));
  for (size_t i = 0; i < rgb.size(); i += 3) {
    rgb[i] = 255;
    rgb[i + 1] = 0;
    rgb[i + 2] = 0;
  }

  tjhandle compressor = tjInitCompress();
  unsigned char* jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;  // NOLINT(google-runtime-int)
  tjCompress2(
      compressor, rgb.data(), width, width * 3, height, TJPF_RGB, &jpeg_buf, &jpeg_size, TJSAMP_420, 80,
      TJFLAG_FASTUPSAMPLE);
  std::vector<uint8_t> result(jpeg_buf, jpeg_buf + jpeg_size);
  tjFree(jpeg_buf);
  tjDestroy(compressor);
  return result;
}

TEST(ImageDecoderTest, DecodeValidJpeg) {
  ImageDecoder decoder;
  auto jpeg = createTestJpeg(64, 48);
  auto result = decoder.decodeJpeg(jpeg.data(), jpeg.size());
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->width, 64);
  EXPECT_EQ(result->height, 48);
  EXPECT_EQ(result->format, PixelFormat::kRGB888);
  EXPECT_FALSE(result->isNull());

  EXPECT_GT((*result->pixels)[0], 200);
  EXPECT_LT((*result->pixels)[1], 50);
  EXPECT_LT((*result->pixels)[2], 50);
}

TEST(ImageDecoderTest, DecodeEmptyInputFails) {
  ImageDecoder decoder;
  auto result = decoder.decodeJpeg(nullptr, 0);
  EXPECT_FALSE(result.has_value());
}

TEST(ImageDecoderTest, DecodeCorruptInputFails) {
  ImageDecoder decoder;
  std::vector<uint8_t> garbage = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xD8, 0xFF, 0xE0};
  auto result = decoder.decodeJpeg(garbage.data(), garbage.size());
  EXPECT_FALSE(result.has_value());
}

TEST(ImageDecoderTest, DecodeCancelledReturnsEarly) {
  ImageDecoder decoder;
  auto jpeg = createTestJpeg(64, 48);
  auto token = makeCancelToken();
  token->cancel();
  auto result = decoder.decodeJpeg(jpeg.data(), jpeg.size(), token);
  EXPECT_FALSE(result.has_value());
}

TEST(ImageDecoderTest, DecodeRawRgb) {
  constexpr int kW = 4;
  constexpr int kH = 4;
  std::vector<uint8_t> raw(kW * kH * 3, 0x80);
  auto result = ImageDecoder::decodeRaw(raw.data(), raw.size(), kW, kH, PixelFormat::kRGB888);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->width, kW);
  EXPECT_EQ(result->height, kH);
  EXPECT_EQ((*result->pixels)[0], 0x80);
}

TEST(ImageDecoderTest, DecodeRawBufferTooSmall) {
  std::vector<uint8_t> raw(10);
  auto result = ImageDecoder::decodeRaw(raw.data(), raw.size(), 100, 100, PixelFormat::kRGB888);
  EXPECT_FALSE(result.has_value());
}

}  // namespace
}  // namespace PJ
