#pragma once

#include <QAbstractItemModel>
#include <QMimeData>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/types.hpp"
#include "pj_datastore/engine.hpp"

namespace proto {

class SeriesTreeModel : public QAbstractItemModel {
  Q_OBJECT

 public:
  explicit SeriesTreeModel(const PJ::DataEngine& engine, QObject* parent = nullptr);

  void rebuild();
  void rebuildIfChanged();

  void setDatasetState(PJ::DatasetId id, PJ_data_source_state_t state);
  void hideDataset(PJ::DatasetId id);
  void clearHidden();

  /// Returns the DatasetId for a dataset-level index, or 0 if not a dataset node.
  [[nodiscard]] PJ::DatasetId datasetIdAt(const QModelIndex& index) const;

  /// Returns true if the index points to a dataset-level node (level 0).
  [[nodiscard]] bool isDatasetNode(const QModelIndex& index) const;

  // QAbstractItemModel interface
  [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
  [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
  [[nodiscard]] QStringList mimeTypes() const override;
  [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;

 private:
  struct FieldNode {
    std::string name;
    PJ::TopicId topic_id = 0;
    size_t col_index = 0;
  };

  struct TopicNode {
    std::string name;
    PJ::TopicId topic_id = 0;
    std::vector<FieldNode> fields;
  };

  struct DatasetNode {
    std::string name;
    PJ::DatasetId dataset_id = 0;
    std::vector<TopicNode> topics;
  };

  const PJ::DataEngine& engine_;
  std::vector<DatasetNode> datasets_;
  std::unordered_map<PJ::DatasetId, PJ_data_source_state_t> dataset_states_;
  std::unordered_set<PJ::DatasetId> hidden_datasets_;
};

}  // namespace proto
