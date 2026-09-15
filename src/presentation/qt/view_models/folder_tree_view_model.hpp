#pragma once

#include "application/use_cases/folders/create_folder.hpp"
#include "application/use_cases/folders/delete_folder.hpp"
#include "application/use_cases/folders/list_folders.hpp"
#include "application/use_cases/folders/rename_folder.hpp"
#include "presentation/qt/execution/use_case_dispatcher.hpp"
#include "presentation/qt/models/folder_tree_model.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

namespace notes::presentation {

class FolderTreeViewModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(FolderTreeModel* model READ model CONSTANT)
  Q_PROPERTY(QString selectedFolderId READ selectedFolderId WRITE
                 setSelectedFolderId NOTIFY selectedFolderIdChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
  // Order matches composition_root.hpp
  FolderTreeViewModel(application::ListFolders& list_folders,
                      application::CreateFolder& create_folder,
                      application::RenameFolder& rename_folder,
                      application::DeleteFolder& delete_folder,
                      UseCaseDispatcher& dispatcher, QObject* parent = nullptr);

  [[nodiscard]] FolderTreeModel* model() const { return model_; }
  [[nodiscard]] QString selectedFolderId() const { return selected_folder_id_; }
  void setSelectedFolderId(const QString& id);
  [[nodiscard]] bool busy() const { return busy_; }
  [[nodiscard]] QString errorString() const { return error_; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void createFolder(const QString& name,
                                const QString& parentId = QString());
  Q_INVOKABLE void renameFolder(const QString& id, const QString& name);
  Q_INVOKABLE void deleteFolder(const QString& id);

signals:
  void selectedFolderIdChanged();
  void busyChanged();
  void errorStringChanged();
  void foldersChanged();

private:
  void setBusy(bool v);
  void setError(QString e);

  application::ListFolders& list_folders_;
  application::CreateFolder& create_folder_;
  application::RenameFolder& rename_folder_;
  application::DeleteFolder& delete_folder_;
  UseCaseDispatcher& dispatcher_;
  FolderTreeModel* model_{nullptr};
  QString selected_folder_id_;
  bool busy_{false};
  QString error_;
};

}  // namespace notes::presentation
