#pragma once

#include "adapters/attachments/filesystem_attachment_store.hpp"
#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_folder_store.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "adapters/system/system_clock.hpp"
#include "adapters/system/xdg_storage_paths.hpp"
#include "application/use_cases/folders/create_folder.hpp"
#include "application/use_cases/folders/delete_folder.hpp"
#include "application/use_cases/folders/list_folders.hpp"
#include "application/use_cases/folders/rename_folder.hpp"
#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/delete_note.hpp"
#include "application/use_cases/notes/list_notes.hpp"
#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/use_cases/notes/search_notes.hpp"
#include "presentation/qt/execution/use_case_dispatcher.hpp"
#include "presentation/qt/qml_support.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"
#include "presentation/qt/view_models/folder_tree_view_model.hpp"
#include "presentation/qt/view_models/note_list_view_model.hpp"

#include <QQmlApplicationEngine>
#include <memory>
#include <stdexcept>
#include <utility>

namespace notes {

class CompositionRoot {
public:
  explicit CompositionRoot(adapters::system::AppPaths paths) : paths_(std::move(paths)) {
    if (!adapters::system::ensure_storage_dirs(paths_)) {
      throw std::runtime_error("failed to create storage directories");
    }
    db_ = std::make_shared<adapters::persistence::SqliteDb>();
    auto opened = db_->open(paths_.db_path);
    if (!opened) {
      throw std::runtime_error(opened.error().message);
    }
    note_store_ = std::make_unique<adapters::persistence::SqliteNoteStore>(db_);
    folder_store_ = std::make_unique<adapters::persistence::SqliteFolderStore>(db_);
    attachments_ = std::make_unique<adapters::attachments::FilesystemAttachmentStore>(
        paths_.attachments_dir, clock_);
    ensure_default_folder();

    create_note_ = std::make_unique<application::CreateNote>(*note_store_, clock_);
    load_note_ = std::make_unique<application::LoadNote>(*note_store_);
    save_note_ = std::make_unique<application::SaveNote>(*note_store_, *note_store_, clock_);
    list_notes_ = std::make_unique<application::ListNotes>(*note_store_);
    search_notes_ = std::make_unique<application::SearchNotes>(*note_store_);
    delete_note_ = std::make_unique<application::DeleteNote>(*note_store_);
    create_folder_ = std::make_unique<application::CreateFolder>(*folder_store_, clock_);
    rename_folder_ =
        std::make_unique<application::RenameFolder>(*folder_store_, *folder_store_, clock_);
    list_folders_ = std::make_unique<application::ListFolders>(*folder_store_);
    delete_folder_ = std::make_unique<application::DeleteFolder>(*folder_store_);

    dispatcher_ = std::make_unique<presentation::UseCaseDispatcher>();
    folder_vm_ = std::make_unique<presentation::FolderTreeViewModel>(
        *list_folders_, *create_folder_, *rename_folder_, *delete_folder_, *dispatcher_);
    note_list_vm_ = std::make_unique<presentation::NoteListViewModel>(
        *list_notes_, *create_note_, *delete_note_, *search_notes_, *dispatcher_);
    editor_vm_ = std::make_unique<presentation::EditorViewModel>(
        *load_note_, *save_note_, *dispatcher_);
  }

  void register_qml(QQmlApplicationEngine& engine) {
    presentation::registerQmlTypes();
    presentation::exposeToQml(engine, folder_vm_.get(), note_list_vm_.get(), editor_vm_.get());
  }

  void flush_and_shutdown() {
    if (editor_vm_) {
      (void)editor_vm_->flushSync();
    }
    if (dispatcher_) {
      dispatcher_->flushAndShutdown();
    }
  }

  presentation::FolderTreeViewModel* folders() const { return folder_vm_.get(); }
  presentation::NoteListViewModel* notes() const { return note_list_vm_.get(); }
  presentation::EditorViewModel* editor() const { return editor_vm_.get(); }

private:
  void ensure_default_folder() {
    auto folders = folder_store_->list_all();
    if (folders && !folders.value().empty()) {
      return;
    }
    domain::Folder f;
    f.id = domain::FolderId{"folder-notes"};
    f.name = "Notes";
    f.created_at_ms = clock_.now_ms();
    f.modified_at_ms = f.created_at_ms;
    auto saved = folder_store_->save(f);
    if (!saved) {
      throw std::runtime_error(saved.error().message);
    }
  }

  adapters::system::AppPaths paths_;
  adapters::system::SystemClock clock_;
  std::shared_ptr<adapters::persistence::SqliteDb> db_;
  std::unique_ptr<adapters::persistence::SqliteNoteStore> note_store_;
  std::unique_ptr<adapters::persistence::SqliteFolderStore> folder_store_;
  std::unique_ptr<adapters::attachments::FilesystemAttachmentStore> attachments_;
  std::unique_ptr<application::CreateNote> create_note_;
  std::unique_ptr<application::LoadNote> load_note_;
  std::unique_ptr<application::SaveNote> save_note_;
  std::unique_ptr<application::ListNotes> list_notes_;
  std::unique_ptr<application::SearchNotes> search_notes_;
  std::unique_ptr<application::DeleteNote> delete_note_;
  std::unique_ptr<application::CreateFolder> create_folder_;
  std::unique_ptr<application::RenameFolder> rename_folder_;
  std::unique_ptr<application::ListFolders> list_folders_;
  std::unique_ptr<application::DeleteFolder> delete_folder_;
  std::unique_ptr<presentation::UseCaseDispatcher> dispatcher_;
  std::unique_ptr<presentation::FolderTreeViewModel> folder_vm_;
  std::unique_ptr<presentation::NoteListViewModel> note_list_vm_;
  std::unique_ptr<presentation::EditorViewModel> editor_vm_;
};

}  // namespace notes
