#include "pj_media_core/codec_pipeline.h"

namespace PJ {

void CodecPipeline::addStage(std::unique_ptr<CodecStage> stage) {
  stages_.push_back(std::move(stage));
}

Expected<DecodedFrame> CodecPipeline::decode(const uint8_t* data, size_t size) const {
  if (stages_.empty()) {
    return unexpected("empty pipeline");
  }

  // Wrap raw bytes into an initial DecodedFrame (no dimensions, no format)
  DecodedFrame input;
  input.pixels = std::make_shared<std::vector<uint8_t>>(data, data + size);

  Expected<DecodedFrame> result = stages_[0]->decode(input);
  if (!result.has_value()) {
    return result;
  }

  for (size_t i = 1; i < stages_.size(); ++i) {
    if (result->isNull()) {
      return unexpected("intermediate stage produced null frame");
    }
    result = stages_[i]->decode(*result);
    if (!result.has_value()) {
      return result;
    }
  }

  return result;
}

}  // namespace PJ
