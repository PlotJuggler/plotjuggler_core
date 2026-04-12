#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace PJ {

struct CompositeFrame {
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  int channels = 0;
};

struct SlotResult {
  int64_t timestamp_ns = 0;
  CompositeFrame frame;
};

class FrameSlot {
 public:
  void store(int64_t timestamp_ns, CompositeFrame frame) {
    std::lock_guard lock(mutex_);
    frame_ = std::move(frame);
    timestamp_ns_ = timestamp_ns;
    has_new_ = true;
  }

  std::optional<SlotResult> take() {
    std::lock_guard lock(mutex_);
    if (!has_new_) {
      return std::nullopt;
    }
    has_new_ = false;
    return SlotResult{timestamp_ns_, std::move(frame_)};
  }

 private:
  std::mutex mutex_;
  CompositeFrame frame_;
  int64_t timestamp_ns_ = 0;
  bool has_new_ = false;
};

}  // namespace PJ
