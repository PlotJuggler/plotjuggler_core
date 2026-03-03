#include "pj_datastore/engine.hpp"

#include <algorithm>
#include <utility>

#include "absl/strings/str_cat.h"
#include "pj_base/assert.hpp"
#include "pj_base/expected.hpp"
#include "pj_datastore/reader.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ {

DataEngine::DataEngine() = default;

// ---------------------------------------------------------------------------
// Dataset management
// ---------------------------------------------------------------------------

Expected<DatasetId> DataEngine::createDataset(DatasetDescriptor descriptor) {
  DatasetId id = next_dataset_id_++;

  // Verify time domain exists if specified
  if (descriptor.time_domain_id != 0) {
    auto it = time_domains_.find(descriptor.time_domain_id);
    if (it == time_domains_.end()) {
      return PJ::unexpected(absl::StrCat("Time domain ", descriptor.time_domain_id, " not found"));
    }
  }

  DatasetInfo info;
  info.id = id;
  info.source_name = std::move(descriptor.source_name);
  if (descriptor.time_domain_id != 0) {
    info.time_domain = time_domains_.at(descriptor.time_domain_id);
  }
  datasets_.emplace(id, std::move(info));
  return id;
}

const DatasetInfo* DataEngine::getDataset(DatasetId id) const {
  auto it = datasets_.find(id);
  if (it == datasets_.end()) {
    return nullptr;
  }
  return &it->second;
}

// ---------------------------------------------------------------------------
// Topic management
// ---------------------------------------------------------------------------

Expected<TopicId> DataEngine::createTopic(DatasetId dataset_id, TopicDescriptor descriptor) {
  auto it = datasets_.find(dataset_id);
  if (it == datasets_.end()) {
    return PJ::unexpected(absl::StrCat("Dataset ", dataset_id, " not found"));
  }

  // Validate schema_id if non-zero (zero means inline columns, e.g. scalar series)
  if (descriptor.schema_id != 0) {
    if (type_registry_.lookup(descriptor.schema_id) == nullptr) {
      return PJ::unexpected(absl::StrCat("Schema ", descriptor.schema_id, " not found"));
    }
  }

  TopicId id = next_topic_id_++;
  descriptor.dataset_id = dataset_id;
  topics_.emplace(
      std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(id, std::move(descriptor)));
  it->second.topic_ids.push_back(id);
  return id;
}

TopicStorage* DataEngine::getTopicStorage(TopicId id) {
  auto it = topics_.find(id);
  if (it == topics_.end()) {
    return nullptr;
  }
  return &it->second;
}

const TopicStorage* DataEngine::getTopicStorage(TopicId id) const {
  auto it = topics_.find(id);
  if (it == topics_.end()) {
    return nullptr;
  }
  return &it->second;
}

// ---------------------------------------------------------------------------
// Schema registry
// ---------------------------------------------------------------------------

TypeRegistry& DataEngine::typeRegistry() {
  return type_registry_;
}

const TypeRegistry& DataEngine::typeRegistry() const {
  return type_registry_;
}

// ---------------------------------------------------------------------------
// Time domains
// ---------------------------------------------------------------------------

Expected<TimeDomainId> DataEngine::createTimeDomain(std::string name) {
  TimeDomainId id = next_time_domain_id_++;
  TimeDomain td;
  td.id = id;
  td.name = std::move(name);
  time_domains_.emplace(id, std::move(td));
  return id;
}

const TimeDomain* DataEngine::getTimeDomain(TimeDomainId id) const {
  auto it = time_domains_.find(id);
  if (it == time_domains_.end()) {
    return nullptr;
  }
  return &it->second;
}

void DataEngine::setDisplayOffset(TimeDomainId id, Timestamp offset) {
  auto it = time_domains_.find(id);
  if (it != time_domains_.end()) {
    it->second.display_offset = offset;
  }
}

// ---------------------------------------------------------------------------
// Commit cycle
// ---------------------------------------------------------------------------

std::vector<TopicId> DataEngine::commitChunks(
    std::vector<std::pair<TopicId, TopicChunk>> chunks) {  // NOLINT(performance-unnecessary-value-param)
  std::vector<TopicId> changed;
  for (auto& [topic_id, chunk] : chunks) {
    auto* storage = getTopicStorage(topic_id);
    if (storage != nullptr) {
      auto status = storage->appendSealedChunk(std::move(chunk));
      PJ_ASSERT(status.has_value(), "out-of-order chunk from writer — writer bug");
      (void)status;  // suppress unused-variable warning in release builds
      if (changed.empty() || changed.back() != topic_id) {
        changed.push_back(topic_id);
      }
    }
  }
  // Deduplicate (flushAll() may emit multiple chunks for one topic).
  std::sort(changed.begin(), changed.end());
  changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
  return changed;
}

void DataEngine::enforceRetention(Timestamp retention_window_ns) {
  for (auto& [topic_id, storage] : topics_) {
    if (!storage.empty()) {
      Timestamp t_max = storage.time_max();
      storage.evictBefore(t_max - retention_window_ns);
    }
  }
}

// ---------------------------------------------------------------------------
// Listing helpers
// ---------------------------------------------------------------------------

std::vector<DatasetId> DataEngine::listDatasets() const {
  std::vector<DatasetId> result;
  result.reserve(datasets_.size());
  for (const auto& [id, info] : datasets_) {
    result.push_back(id);
  }
  return result;
}

std::vector<TopicId> DataEngine::listTopics(DatasetId dataset_id) const {
  auto it = datasets_.find(dataset_id);
  if (it == datasets_.end()) {
    return {};
  }
  return it->second.topic_ids;
}

// ---------------------------------------------------------------------------
// Writer/Reader factories
// ---------------------------------------------------------------------------

DataWriter DataEngine::createWriter() {
  return DataWriter(*this);
}

DataReader DataEngine::createReader() const {
  return DataReader(*this);
}

}  // namespace PJ
