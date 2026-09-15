#include "presentation/qt/view_models/folder_tree_view_model.hpp"

#include "presentation/qt/view_models/error_text.hpp"

namespace notes::presentation {

FolderTreeViewModel::FolderTreeViewModel(
    application::ListFolders& list_folders,
    application::CreateFolder& create_folder,
    application::RenameFolder& rename_folder,
    application::DeleteFolder& delete_folder, UseCaseDispatcher& dispatcher,
    QObject* parent)
    : QObject(parent),
      list_folders_(list_folders),
      create_folder_(create_folder),
      rename_folder_(rename_folder),
      delete_folder_(delete_folder),
      dispatcher_(dispatcher),
      model_(new FolderTreeModel(this)) {}

void FolderTreeViewModel::setSelectedFolderId(const QString& id) {
  if (selected_folder_id_ == id) return;
  selected_folder_id_ = id;
  emit selectedFolderIdChanged();
}

void FolderTreeViewModel::setBusy(bool v) {
  if (busy_ == v) return;
  busy_ = v;
  emit busyChanged();
}

void FolderTreeViewModel::setError(QString e) {
  if (error_ == e) return;
  error_ = std::move(e);
  emit errorStringChanged();
}

void FolderTreeViewModel::refresh() {
  setBusy(true);
  setError({});
  QPointer<FolderTreeViewModel> self(this);
  dispatcher_.postResult<application::Result<std::vector<domain::Folder>>>(
      [this]() { return list_folders_.execute(); }, this,
      [self](application::Result<std::vector<domain::Folder>> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        auto folders = std::move(result.value());
        self->model_->setFolders(folders);
        if (self->selected_folder_id_.isEmpty() && !folders.empty()) {
          std::string pick = folders.front().id.value();
          for (const auto& f : folders) {
            if (!f.parent_id.has_value() || f.parent_id->empty()) {
              pick = f.id.value();
              break;
            }
          }
          self->setSelectedFolderId(QString::fromStdString(pick));
        }
        emit self->foldersChanged();
      });
}

void FolderTreeViewModel::createFolder(const QString& name,
                                       const QString& parentId) {
  application::CreateFolder::Request req;
  req.name = name.trimmed().toStdString();
  if (!parentId.isEmpty()) {
    req.parent_id = domain::FolderId{parentId.toStdString()};
  }
  setBusy(true);
  setError({});
  QPointer<FolderTreeViewModel> self(this);
  dispatcher_.postResult<application::Result<domain::Folder>>(
      [this, req = std::move(req)]() mutable {
        return create_folder_.execute(std::move(req));
      },
      this, [self](application::Result<domain::Folder> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        self->setSelectedFolderId(
            QString::fromStdString(result.value().id.value()));
        self->refresh();
      });
}

void FolderTreeViewModel::renameFolder(const QString& id, const QString& name) {
  application::RenameFolder::Request req;
  req.id = domain::FolderId{id.toStdString()};
  req.name = name.trimmed().toStdString();
  setBusy(true);
  setError({});
  QPointer<FolderTreeViewModel> self(this);
  dispatcher_.postResult<application::Result<domain::Folder>>(
      [this, req = std::move(req)]() mutable {
        return rename_folder_.execute(std::move(req));
      },
      this, [self](application::Result<domain::Folder> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        self->refresh();
      });
}

void FolderTreeViewModel::deleteFolder(const QString& id) {
  const domain::FolderId fid{id.toStdString()};
  setBusy(true);
  setError({});
  QPointer<FolderTreeViewModel> self(this);
  dispatcher_.postResult<application::Result<void>>(
      [this, fid]() { return delete_folder_.execute(fid); }, this,
      [self, id](application::Result<void> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        if (self->selected_folder_id_ == id) {
          self->setSelectedFolderId({});
        }
        self->refresh();
      });
}

}  // namespace notes::presentation
