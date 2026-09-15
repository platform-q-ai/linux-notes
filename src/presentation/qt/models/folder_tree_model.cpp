#include "presentation/qt/models/folder_tree_model.hpp"

#include <algorithm>

namespace notes::presentation {

FolderTreeModel::FolderTreeModel(QObject* parent) : QAbstractItemModel(parent) {}

void FolderTreeModel::setFolders(std::vector<domain::Folder> folders) {
  beginResetModel();
  rebuildTree(std::move(folders));
  endResetModel();
}

void FolderTreeModel::rebuildTree(std::vector<domain::Folder> folders) {
  nodes_.clear();
  root_indices_.clear();
  id_to_index_.clear();

  nodes_.reserve(static_cast<int>(folders.size()));
  for (auto& f : folders) {
    const QString id = QString::fromStdString(f.id.value());
    FolderNode node;
    node.folder = std::move(f);
    const int idx = nodes_.size();
    nodes_.push_back(std::move(node));
    id_to_index_.insert(id, idx);
  }

  for (int i = 0; i < nodes_.size(); ++i) {
    const auto& parent = nodes_[i].folder.parent_id;
    if (!parent.has_value() || parent->empty()) {
      root_indices_.push_back(i);
      nodes_[i].parent_index = -1;
      continue;
    }
    const QString pid = QString::fromStdString(parent->value());
    const auto it = id_to_index_.constFind(pid);
    if (it == id_to_index_.cend()) {
      root_indices_.push_back(i);
      nodes_[i].parent_index = -1;
    } else {
      const int p = it.value();
      nodes_[i].parent_index = p;
      nodes_[p].child_indices.push_back(i);
    }
  }

  auto by_order = [this](int a, int b) {
    const auto oa = nodes_[a].folder.sort_order;
    const auto ob = nodes_[b].folder.sort_order;
    if (oa != ob) {
      return oa < ob;
    }
    return nodes_[a].folder.name < nodes_[b].folder.name;
  };
  std::sort(root_indices_.begin(), root_indices_.end(), by_order);
  for (auto& n : nodes_) {
    std::sort(n.child_indices.begin(), n.child_indices.end(), by_order);
  }

  // Flat list exposure for ListView: all folders as top-level rows, depth via
  // parentId role. Tree parent()/index() remain valid for future TreeView.
  root_indices_.clear();
  root_indices_.reserve(nodes_.size());
  for (int i = 0; i < nodes_.size(); ++i) {
    root_indices_.push_back(i);
  }
  std::sort(root_indices_.begin(), root_indices_.end(), by_order);
}

domain::FolderId FolderTreeModel::folderIdAt(const QModelIndex& index) const {
  if (!index.isValid()) {
    return {};
  }
  const int i = static_cast<int>(index.internalId());
  if (i < 0 || i >= nodes_.size()) {
    return {};
  }
  return nodes_[i].folder.id;
}

QModelIndex FolderTreeModel::indexForFolderId(const domain::FolderId& id) const {
  const auto it = id_to_index_.constFind(QString::fromStdString(id.value()));
  if (it == id_to_index_.cend()) {
    return {};
  }
  const int i = it.value();
  const int parent = nodes_[i].parent_index;
  const QVector<int>& siblings =
      parent < 0 ? root_indices_ : nodes_[parent].child_indices;
  const int row = siblings.indexOf(i);
  if (row < 0) {
    return {};
  }
  return createIndex(row, 0, static_cast<quintptr>(i));
}

QModelIndex FolderTreeModel::index(int row, int column,
                                   const QModelIndex& parent) const {
  if (row < 0 || column != 0) {
    return {};
  }
  if (!parent.isValid()) {
    if (row >= root_indices_.size()) {
      return {};
    }
    return createIndex(row, 0, static_cast<quintptr>(root_indices_[row]));
  }
  const int pi = static_cast<int>(parent.internalId());
  if (pi < 0 || pi >= nodes_.size()) {
    return {};
  }
  const auto& kids = nodes_[pi].child_indices;
  if (row >= kids.size()) {
    return {};
  }
  return createIndex(row, 0, static_cast<quintptr>(kids[row]));
}

QModelIndex FolderTreeModel::parent(const QModelIndex& child) const {
  if (!child.isValid()) {
    return {};
  }
  const int i = static_cast<int>(child.internalId());
  if (i < 0 || i >= nodes_.size()) {
    return {};
  }
  const int p = nodes_[i].parent_index;
  if (p < 0) {
    return {};
  }
  const int gp = nodes_[p].parent_index;
  const QVector<int>& siblings =
      gp < 0 ? root_indices_ : nodes_[gp].child_indices;
  const int row = siblings.indexOf(p);
  if (row < 0) {
    return {};
  }
  return createIndex(row, 0, static_cast<quintptr>(p));
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const {
  if (!parent.isValid()) {
    return root_indices_.size();
  }
  const int pi = static_cast<int>(parent.internalId());
  if (pi < 0 || pi >= nodes_.size()) {
    return 0;
  }
  return nodes_[pi].child_indices.size();
}

int FolderTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return {};
  }
  const int i = static_cast<int>(index.internalId());
  if (i < 0 || i >= nodes_.size()) {
    return {};
  }
  const auto& f = nodes_[i].folder;
  switch (role) {
    case Qt::DisplayRole:
    case NameRole:
      return QString::fromStdString(f.name);
    case IdRole:
      return QString::fromStdString(f.id.value());
    case ParentIdRole:
      return f.parent_id ? QString::fromStdString(f.parent_id->value())
                         : QString();
    case SortOrderRole:
      return static_cast<qint64>(f.sort_order);
    default:
      return {};
  }
}

QHash<int, QByteArray> FolderTreeModel::roleNames() const {
  return {
      {IdRole, "folderId"},
      {NameRole, "name"},
      {ParentIdRole, "parentId"},
      {SortOrderRole, "sortOrder"},
      {Qt::DisplayRole, "display"},
  };
}

}  // namespace notes::presentation
