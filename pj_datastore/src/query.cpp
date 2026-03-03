#include "pj_datastore/query.hpp"

#include <cassert>

namespace PJ {

// ===========================================================================
// RangeCursor
// ===========================================================================

RangeCursor::RangeCursor(const std::deque<TopicChunk>& chunks, Timestamp t_min, Timestamp t_max)
    : chunks_(&chunks), t_min_(t_min), t_max_(t_max) {
  findFirstValid();
}

bool RangeCursor::valid() const noexcept {
  return chunk_index_ < chunks_->size();
}

SampleRow RangeCursor::current() const {
  assert(valid());
  const auto& chunk = (*chunks_)[chunk_index_];
  return SampleRow{chunk.readTimestamp(row_index_), &chunk, row_index_};
}

void RangeCursor::advance() {
  assert(valid());
  const auto& chunk = (*chunks_)[chunk_index_];
  ++row_index_;
  if (row_index_ >= chunk.stats.row_count) {
    ++chunk_index_;
    row_index_ = 0;
  }
  skipToValid();
}

void RangeCursor::forEach(std::function<void(const SampleRow&)> callback) {
  while (valid()) {
    callback(current());
    advance();
  }
}

void RangeCursor::forEachChunk(std::function<void(const ChunkRowRange&)> callback) {
  while (chunk_index_ < chunks_->size()) {
    const auto& chunk = (*chunks_)[chunk_index_];

    // Skip chunks entirely before our range
    if (chunk.stats.t_max < t_min_) {
      ++chunk_index_;
      continue;
    }
    // Stop if chunk is entirely after our range
    if (chunk.stats.t_min > t_max_) {
      break;
    }

    // Find first valid row in this chunk (>= t_min_)
    std::size_t first = row_index_;
    while (first < chunk.stats.row_count && chunk.readTimestamp(first) < t_min_) {
      ++first;
    }

    // Find one-past-last valid row in this chunk (<= t_max_)
    std::size_t end = first;
    while (end < chunk.stats.row_count && chunk.readTimestamp(end) <= t_max_) {
      ++end;
    }

    if (first < end) {
      callback(ChunkRowRange{&chunk, first, end});
    }

    // Move to next chunk
    ++chunk_index_;
    row_index_ = 0;
  }
  // Mark cursor exhausted
  chunk_index_ = chunks_->size();
}

void RangeCursor::findFirstValid() {
  // Linear scan to find the first chunk whose t_max >= t_min_
  // (i.e., that could contain data in our range)
  for (chunk_index_ = 0; chunk_index_ < chunks_->size(); ++chunk_index_) {
    const auto& chunk = (*chunks_)[chunk_index_];
    if (chunk.stats.t_max >= t_min_) {
      // This chunk might contain data in range.
      // Now find the first row where timestamp >= t_min_.
      for (row_index_ = 0; row_index_ < chunk.stats.row_count; ++row_index_) {
        Timestamp ts = chunk.readTimestamp(row_index_);
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

void RangeCursor::skipToValid() {
  if (!valid()) {
    return;
  }
  const auto& chunk = (*chunks_)[chunk_index_];
  Timestamp ts = chunk.readTimestamp(row_index_);
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

std::optional<SampleRow> latestAt(const std::deque<TopicChunk>& chunks, Timestamp t) {
  if (chunks.empty()) {
    return std::nullopt;
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
      Timestamp ts = chunk.readTimestamp(ri - 1);
      if (ts <= t) {
        return SampleRow{ts, &chunk, ri - 1};
      }
    }
    // All rows in this chunk are after t, but t_min <= t was true.
    // This shouldn't happen with sorted data, but handle gracefully
    // by continuing to the previous chunk.
  }

  return std::nullopt;
}

// ===========================================================================
// range_query
// ===========================================================================

RangeCursor rangeQuery(const std::deque<TopicChunk>& chunks, Timestamp t_min, Timestamp t_max) {
  return RangeCursor(chunks, t_min, t_max);
}

}  // namespace PJ
