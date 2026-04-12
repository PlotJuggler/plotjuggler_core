#pragma once

#include <atomic>
#include <memory>

namespace PJ {

class CancelToken {
 public:
  [[nodiscard]] bool isCancelled() const {
    return flag_.load(std::memory_order_relaxed);
  }

  void cancel() {
    flag_.store(true, std::memory_order_relaxed);
  }

 private:
  std::atomic<bool> flag_{false};
};

using CancelTokenPtr = std::shared_ptr<CancelToken>;

[[nodiscard]] inline CancelTokenPtr makeCancelToken() {
  return std::make_shared<CancelToken>();
}

}  // namespace PJ
