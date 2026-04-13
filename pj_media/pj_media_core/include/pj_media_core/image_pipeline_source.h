#pragma once

#include <memory>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/codec_pipeline.h"
#include "pj_media_core/media_source.h"

namespace PJ {

/// MediaSource for image topics. Wraps a CodecPipeline and ObjectStore.
/// Decodes synchronously in setTimestamp() — JPEG at 1080p is <10ms.
class ImagePipelineSource : public MediaSource {
 public:
  ImagePipelineSource(ObjectStore* store, ObjectTopicId topic, std::unique_ptr<CodecPipeline> pipeline);

  void setTimestamp(int64_t ts_ns) override;
  std::optional<DecodedFrame> takeFrame() override;

 private:
  ObjectStore* store_;
  ObjectTopicId topic_;
  std::unique_ptr<CodecPipeline> pipeline_;

  std::optional<DecodedFrame> pending_frame_;
};

}  // namespace PJ
