#include "pj/engine/query.hpp"

#include <cassert>

namespace pj::engine {

// ===========================================================================
// RangeCursor
// ===========================================================================

RangeCursor::RangeCursor(const std::deque<TopicChunk>& chunks,
                         Timestamp t_min, Timestamp t_max)
    : chunks_(&chunks), t_min_(t_min), t_max_(t_max) {
  find_first_valid();
}

bool RangeCursor::valid() const noexcept {
  return chunk_index_ < chunks_->size();
}

SampleRow RangeCursor::current() const {
  assert(valid());
  const auto& chunk = (*chunks_)[chunk_index_];
  return SampleRow{chunk.read_timestamp(row_index_), &chunk, row_index_};
}

void RangeCursor::advance() {
  assert(valid());
  const auto& chunk = (*chunks_)[chunk_index_];
  ++row_index_;
  if (row_index_ >= chunk.stats.row_count) {
    ++chunk_index_;
    row_index_ = 0;
  }
  skip_to_valid();
}

void RangeCursor::for_each(
    absl::FunctionRef<void(const SampleRow&)> callback) {
  while (valid()) {
    callback(current());
    advance();
  }
}

void RangeCursor::find_first_valid() {
  // Linear scan to find the first chunk whose t_max >= t_min_
  // (i.e., that could contain data in our range)
  for (chunk_index_ = 0; chunk_index_ < chunks_->size(); ++chunk_index_) {
    const auto& chunk = (*chunks_)[chunk_index_];
    if (chunk.stats.t_max >= t_min_) {
      // This chunk might contain data in range.
      // Now find the first row where timestamp >= t_min_.
      for (row_index_ = 0; row_index_ < chunk.stats.row_count; ++row_index_) {
        Timestamp ts = chunk.read_timestamp(row_index_);
        if (ts >= t_min_) {
          // Check if this row is also within t_max
          if (ts <= t_max_) {
            return;  // Found a valid starting position
          }
          // ts > t_max_ means no valid data in range at all
          chunk_index_ = chunks_->size();
          return;
        }
      }
      // All rows in this chunk are before t_min, try next chunk
      continue;
    }
  }
  // No valid chunk found: chunk_index_ == chunks_->size() (past-end)
}

void RangeCursor::skip_to_valid() {
  if (!valid()) {
    return;
  }
  const auto& chunk = (*chunks_)[chunk_index_];
  Timestamp ts = chunk.read_timestamp(row_index_);
  if (ts > t_max_) {
    // Past the end of the query range
    chunk_index_ = chunks_->size();
    return;
  }
  // ts >= t_min_ is guaranteed by how we advance through sorted data
}

// ===========================================================================
// latest_at
// ===========================================================================

LatestAtResult latest_at(const std::deque<TopicChunk>& chunks, Timestamp t) {
  if (chunks.empty()) {
    return {};
  }

  // Reverse iterate chunks. For each chunk, if t_min <= t, search within it.
  for (std::size_t ci = chunks.size(); ci > 0; --ci) {
    const auto& chunk = chunks[ci - 1];
    if (chunk.stats.t_min > t) {
      continue;  // Entire chunk is after t
    }
    // chunk.stats.t_min <= t, so there might be a row <= t in this chunk.
    // Reverse scan within the chunk to find the last row with timestamp <= t.
    for (std::size_t ri = chunk.stats.row_count; ri > 0; --ri) {
      Timestamp ts = chunk.read_timestamp(ri - 1);
      if (ts <= t) {
        return LatestAtResult{true, ts, &chunk, ri - 1};
      }
    }
    // All rows in this chunk are after t, but t_min <= t was true.
    // This shouldn't happen with sorted data, but handle gracefully
    // by continuing to the previous chunk.
  }

  return {};
}

// ===========================================================================
// range_query
// ===========================================================================

RangeCursor range_query(const std::deque<TopicChunk>& chunks,
                        Timestamp t_min, Timestamp t_max) {
  return RangeCursor(chunks, t_min, t_max);
}

}  // namespace pj::engine
