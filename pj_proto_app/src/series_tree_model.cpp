#include "series_tree_model.hpp"

#include <QDataStream>
#include <QIODevice>

#include "pj_datastore/reader.hpp"

namespace proto {

static constexpr quintptr kNoParent = 0xFFFFFFFF;

SeriesTreeModel::SeriesTreeModel(const PJ::DataEngine& engine, QObject* parent)
    : QAbstractItemModel(parent), engine_(engine) {}

void SeriesTreeModel::rebuild() {
  beginResetModel();
  datasets_.clear();

  auto reader = engine_.createReader();
  for (auto ds_id : reader.listDatasets()) {
    DatasetNode ds_node;
    ds_node.dataset_id = ds_id;
    auto* ds_info = engine_.getDataset(ds_id);
    ds_node.name = ds_info ? ds_info->source_name : ("dataset_" + std::to_string(ds_id));

    for (auto topic_id : reader.listTopics(ds_id)) {
      TopicNode topic_node;
      topic_node.topic_id = topic_id;

      auto meta = reader.getMetadata(topic_id);
      topic_node.name = meta ? meta->name : ("topic_" + std::to_string(topic_id));

      auto* storage = engine_.getTopicStorage(topic_id);
      if (storage) {
        const auto& col_descs = storage->columnDescriptors();
        for (size_t i = 0; i < col_descs.size(); ++i) {
          FieldNode field;
          field.name = col_descs[i].field_path;
          field.topic_id = topic_id;
          field.col_index = i;
          topic_node.fields.push_back(std::move(field));
        }
      }

      ds_node.topics.push_back(std::move(topic_node));
    }

    datasets_.push_back(std::move(ds_node));
  }

  endResetModel();
}

// Internal ID encoding:
// Level 0 (dataset): internalId = kNoParent
// Level 1 (topic): internalId = dataset_index
// Level 2 (field): internalId = (dataset_index << 16) | topic_index | 0x80000000

QModelIndex SeriesTreeModel::index(int row, int column, const QModelIndex& parent) const {
  if (!hasIndex(row, column, parent)) return {};

  if (!parent.isValid()) {
    // Level 0: dataset
    return createIndex(row, column, kNoParent);
  }

  auto parent_id = parent.internalId();
  if (parent_id == kNoParent) {
    // Level 1: topic — parent is dataset at parent.row()
    return createIndex(row, column, static_cast<quintptr>(parent.row()));
  }

  if ((parent_id & 0x80000000u) == 0) {
    // Level 2: field — parent is topic
    auto ds_idx = static_cast<quintptr>(parent_id);
    auto topic_idx = static_cast<quintptr>(parent.row());
    return createIndex(row, column, static_cast<quintptr>(0x80000000u | (ds_idx << 16) | topic_idx));
  }

  return {};
}

QModelIndex SeriesTreeModel::parent(const QModelIndex& child) const {
  if (!child.isValid()) return {};

  auto id = child.internalId();
  if (id == kNoParent) return {};  // dataset has no parent

  if ((id & 0x80000000u) == 0) {
    // Topic: parent is dataset at index `id`
    return createIndex(static_cast<int>(id), 0, kNoParent);
  }

  // Field: decode dataset and topic index
  auto ds_idx = static_cast<int>((id >> 16) & 0x7FFF);
  auto topic_idx = static_cast<int>(id & 0xFFFF);
  return createIndex(topic_idx, 0, static_cast<quintptr>(ds_idx));
}

int SeriesTreeModel::rowCount(const QModelIndex& parent) const {
  if (!parent.isValid()) return static_cast<int>(datasets_.size());

  auto id = parent.internalId();
  if (id == kNoParent) {
    // Dataset → topic count
    auto ds_row = static_cast<size_t>(parent.row());
    return ds_row < datasets_.size() ? static_cast<int>(datasets_[ds_row].topics.size()) : 0;
  }

  if ((id & 0x80000000u) == 0) {
    // Topic → field count
    auto ds_idx = static_cast<size_t>(id);
    auto topic_row = static_cast<size_t>(parent.row());
    if (ds_idx < datasets_.size() && topic_row < datasets_[ds_idx].topics.size()) {
      return static_cast<int>(datasets_[ds_idx].topics[topic_row].fields.size());
    }
    return 0;
  }

  return 0;  // fields have no children
}

int SeriesTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant SeriesTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || role != Qt::DisplayRole) return {};

  auto id = index.internalId();
  if (id == kNoParent) {
    auto row = static_cast<size_t>(index.row());
    return row < datasets_.size() ? QString::fromStdString(datasets_[row].name) : QVariant();
  }

  if ((id & 0x80000000u) == 0) {
    auto ds_idx = static_cast<size_t>(id);
    auto row = static_cast<size_t>(index.row());
    if (ds_idx < datasets_.size() && row < datasets_[ds_idx].topics.size()) {
      return QString::fromStdString(datasets_[ds_idx].topics[row].name);
    }
    return {};
  }

  // Field
  auto ds_idx = static_cast<size_t>((id >> 16) & 0x7FFF);
  auto topic_idx = static_cast<size_t>(id & 0xFFFF);
  auto row = static_cast<size_t>(index.row());
  if (ds_idx < datasets_.size() && topic_idx < datasets_[ds_idx].topics.size() &&
      row < datasets_[ds_idx].topics[topic_idx].fields.size()) {
    return QString::fromStdString(datasets_[ds_idx].topics[topic_idx].fields[row].name);
  }
  return {};
}

Qt::ItemFlags SeriesTreeModel::flags(const QModelIndex& index) const {
  auto base_flags = QAbstractItemModel::flags(index);
  if (!index.isValid()) return base_flags;

  auto id = index.internalId();
  if ((id & 0x80000000u) != 0) {
    // Leaf fields are draggable
    return base_flags | Qt::ItemIsDragEnabled;
  }
  return base_flags;
}

QStringList SeriesTreeModel::mimeTypes() const { return {"application/x-pj-field"}; }

QMimeData* SeriesTreeModel::mimeData(const QModelIndexList& indexes) const {
  if (indexes.isEmpty()) return nullptr;

  auto idx = indexes.first();
  auto id = idx.internalId();
  if ((id & 0x80000000u) == 0) return nullptr;

  auto ds_idx = static_cast<size_t>((id >> 16) & 0x7FFF);
  auto topic_idx = static_cast<size_t>(id & 0xFFFF);
  auto row = static_cast<size_t>(idx.row());

  if (ds_idx >= datasets_.size() || topic_idx >= datasets_[ds_idx].topics.size() ||
      row >= datasets_[ds_idx].topics[topic_idx].fields.size()) {
    return nullptr;
  }

  const auto& field = datasets_[ds_idx].topics[topic_idx].fields[row];

  auto* mime = new QMimeData();
  QByteArray encoded;
  QDataStream stream(&encoded, QIODevice::WriteOnly);
  stream << static_cast<quint32>(field.topic_id) << static_cast<quint32>(field.col_index)
         << QString::fromStdString(field.name);
  mime->setData("application/x-pj-field", encoded);
  return mime;
}

}  // namespace proto
