#include "pj_datastore/reader.hpp"

#include <deque>
#include <optional>
#include <vector>

#include "absl/strings/str_cat.h"
#include "pj_base/expected.hpp"
#include "pj_datastore/chunk.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/query.hpp"
#include "pj_datastore/topic_storage.hpp"
#include "pj_datastore/type_registry.hpp"

namespace PJ {

DataReader::DataReader(const DataEngine& engine) : engine_(engine) {}

std::vector<DatasetId> DataReader::list_datasets() const {
  return engine_.list_datasets();
}

std::vector<TopicId> DataReader::list_topics(DatasetId dataset_id) const {
  return engine_.list_topics(dataset_id);
}

const TypeTreeNode* DataReader::get_type_tree(TopicId topic_id) const {
  const TopicStorage* storage = engine_.get_topic_storage(topic_id);
  if (storage == nullptr) {
    return nullptr;
  }
  SchemaId schema_id = storage->descriptor().schema_id;
  return engine_.type_registry().lookup(schema_id);
}

std::optional<TopicMetadata> DataReader::get_metadata(TopicId topic_id) const {
  const TopicStorage* storage = engine_.get_topic_storage(topic_id);
  if (storage == nullptr) {
    return std::nullopt;
  }
  return storage->metadata();
}

Expected<RangeCursor> DataReader::range_query(const QueryRange& range) const {
  const TopicStorage* storage = engine_.get_topic_storage(range.topic_id);
  if (storage == nullptr) {
    return PJ::unexpected(absl::StrCat("Topic ", range.topic_id, " not found"));
  }
  return PJ::range_query(storage->sealed_chunks(), range.t_min, range.t_max);
}

PJ::Expected<std::optional<SampleRow>> DataReader::latest_at(const QueryPoint& point) const {
  const TopicStorage* storage = engine_.get_topic_storage(point.topic_id);
  if (storage == nullptr) {
    return PJ::unexpected(absl::StrCat("Topic ", point.topic_id, " not found"));
  }
  return PJ::latest_at(storage->sealed_chunks(), point.t);
}

}  // namespace PJ
