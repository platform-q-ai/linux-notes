#pragma once

#include "domain/folders/folder.hpp"

#include <QAbstractItemModel>
#include <QHash>
#include <QVector>

#include <vector>

namespace notes::presentation {

class FolderTreeModel : public QAbstractItemModel {
  Q_OBJECT
public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    NameRole,
    ParentIdRole,
    SortOrderRole
  };

  explicit FolderTreeModel(QObject* parent = nullptr);

  QModelIndex index(int row, int column,
                    const QModelIndex& parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setFolders(std::vector<domain::Folder> folders);
  [[nodiscard]] domain::FolderId folderIdAt(const QModelIndex& index) const;
  [[nodiscard]] QModelIndex indexForFolderId(const domain::FolderId& id) const;

private:
  struct FolderNode {
    domain::Folder folder;
    int parent_index{-1};
    QVector<int> child_indices;
  };

  void rebuildTree(std::vector<domain::Folder> folders);

  QVector<FolderNode> nodes_;
  QVector<int> root_indices_;
  QHash<QString, int> id_to_index_;
};

}  // namespace notes::presentation
