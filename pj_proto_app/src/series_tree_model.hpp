#pragma once

#include <QAbstractItemModel>
#include <QMimeData>
#include <string>
#include <vector>

#include "pj_base/types.hpp"
#include "pj_datastore/engine.hpp"

namespace proto {

class SeriesTreeModel : public QAbstractItemModel {
  Q_OBJECT

 public:
  explicit SeriesTreeModel(const PJ::DataEngine& engine, QObject* parent = nullptr);

  void rebuild();

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
};

}  // namespace proto
