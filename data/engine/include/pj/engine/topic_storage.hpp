#pragma once
#include <cstdint>
#include <deque>
#include <string>

#include "absl/status/status.h"

#include "pj/base/types.hpp"
#include "pj/engine/chunk.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::DatasetId;
using pj::Timestamp;
using pj::TopicId;
using pj::SchemaId;

struct TopicDescriptor {
  std::string name;
  SchemaId schema_id = 0;
  DatasetId dataset_id = 0;
  uint32_t max_chunk_rows = 1024;  // Default chunk size
};

struct TopicMetadata {
  TopicId topic_id = 0;
  std::string name;
  SchemaId current_schema = 0;
  DatasetId dataset_id = 0;
  Timestamp time_range_min = 0;
  Timestamp time_range_max = 0;
  uint64_t total_row_count = 0;
  uint64_t total_byte_size = 0;  // approximate
};

class TopicStorage {
public:
  TopicStorage(TopicId topic_id, TopicDescriptor descriptor);

  [[nodiscard]] absl::Status append_sealed_chunk(TopicChunk chunk);
  void evict_before(Timestamp t_keep_min);

  [[nodiscard]] const std::deque<TopicChunk>& sealed_chunks() const noexcept;
  [[nodiscard]] TopicMetadata metadata() const;
  [[nodiscard]] const TopicDescriptor& descriptor() const noexcept;
  [[nodiscard]] TopicId topic_id() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  [[nodiscard]] Timestamp time_min() const noexcept;
  [[nodiscard]] Timestamp time_max() const noexcept;

  void update_schema(SchemaId new_schema);

private:
  TopicId topic_id_;
  TopicDescriptor descriptor_;
  std::deque<TopicChunk> sealed_chunks_;
};

}  // namespace pj::engine
