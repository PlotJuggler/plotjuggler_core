#pragma once
#include <cstddef>
#include <deque>
#include <vector>
#include "absl/functional/function_ref.h"
#include "pj/base/types.hpp"
#include "pj/engine/chunk.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::Timestamp;
using pj::TopicId;

struct QueryRange {
  TopicId topic_id = 0;
  Timestamp t_min = 0;
  Timestamp t_max = 0;
};

struct QueryPoint {
  TopicId topic_id = 0;
  Timestamp t = 0;
};

struct SampleRow {
  Timestamp timestamp = 0;
  const TopicChunk* chunk = nullptr;
  std::size_t row_index = 0;
};

struct ChunkRowRange {
  const TopicChunk* chunk = nullptr;
  std::size_t row_start = 0;
  std::size_t row_end = 0;  // exclusive
};

// Cursor for iterating range query results across chunks
class RangeCursor {
public:
  RangeCursor(const std::deque<TopicChunk>& chunks,
              Timestamp t_min, Timestamp t_max);

  [[nodiscard]] bool valid() const noexcept;
  void advance();
  [[nodiscard]] SampleRow current() const;

  // Iterate all results via callback (per-row)
  void for_each(absl::FunctionRef<void(const SampleRow&)> callback);

  // Iterate chunk-at-a-time (bulk path)
  void for_each_chunk(absl::FunctionRef<void(const ChunkRowRange&)> callback);

private:
  const std::deque<TopicChunk>* chunks_;
  Timestamp t_min_;
  Timestamp t_max_;
  std::size_t chunk_index_ = 0;
  std::size_t row_index_ = 0;

  void find_first_valid();
  void skip_to_valid();
};

struct LatestAtResult {
  bool found = false;
  Timestamp timestamp = 0;
  const TopicChunk* chunk = nullptr;
  std::size_t row_index = 0;
};

// Find the most recent sample at or before time t
[[nodiscard]] LatestAtResult latest_at(
    const std::deque<TopicChunk>& chunks, Timestamp t);

// Create a range cursor
[[nodiscard]] RangeCursor range_query(
    const std::deque<TopicChunk>& chunks,
    Timestamp t_min, Timestamp t_max);

}  // namespace pj::engine
