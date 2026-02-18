#pragma once
#include <optional>
#include <vector>

#include "pj/base/expected.hpp"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/query.hpp"
#include "pj/engine/topic_storage.hpp"

namespace pj::engine {

class DataEngine;

/// Read-only facade over committed DataEngine storage.
/// Provides listing, metadata, type-tree lookup, range queries,
/// and latest-at point queries.
class DataReader {
 public:
  /// Create a read-only facade bound to `engine`.
  explicit DataReader(const DataEngine& engine);

  /// List all dataset ids known by the engine.
  [[nodiscard]] std::vector<pj::DatasetId> list_datasets() const;

  /// List topic ids for one dataset.
  [[nodiscard]] std::vector<pj::TopicId> list_topics(pj::DatasetId dataset_id) const;

  /// Lookup schema tree for a topic (nullptr if unknown).
  [[nodiscard]] const pj::TypeTreeNode* get_type_tree(pj::TopicId topic_id) const;

  /// Return topic metadata if topic exists.
  [[nodiscard]] std::optional<TopicMetadata> get_metadata(pj::TopicId topic_id) const;

  /// Create range cursor over [t_min, t_max].
  [[nodiscard]] pj::Expected<RangeCursor> range_query(const QueryRange& range) const;

  /// Return latest sample at or before query time; nullopt payload if no row exists.
  [[nodiscard]] pj::Expected<std::optional<SampleRow>> latest_at(const QueryPoint& point) const;

 private:
  const DataEngine& engine_;
};

}  // namespace pj::engine
