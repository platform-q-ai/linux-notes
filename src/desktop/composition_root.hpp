#pragma once

#include "adapters/attachments/filesystem_attachment_store.hpp"
#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_folder_store.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "adapters/system/random_id_source.hpp"
#include "adapters/system/system_clock.hpp"
#include "adapters/system/xdg_storage_paths.hpp"
#include "application/use_cases/folders/create_folder.hpp"
#include "application/use_cases/folders/delete_folder.hpp"
#include "application/use_cases/folders/list_folders.hpp"
#include "application/use_cases/attachments/attach_file.hpp"
#include "application/use_cases/folders/rename_folder.hpp"
#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/delete_note.hpp"
#include "application/use_cases/notes/list_notes.hpp"
#include "application/use_cases/notes/list_trashed_notes.hpp"
#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/move_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/restore_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/use_cases/notes/search_notes.hpp"
#include "application/use_cases/notes/toggle_checklist_item.hpp"
#include "application/use_cases/notes/trash_note.hpp"
#include "presentation/qt/app_exit_gate.hpp"
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
    // Attachments store enables keep-both deep-copy of blob ids (lifetime policy).
    save_note_ = std::make_unique<application::SaveNote>(
        *note_store_, *note_store_, clock_, &id_source_, attachments_.get());
    list_notes_ = std::make_unique<application::ListNotes>(*note_store_);
    search_notes_ = std::make_unique<application::SearchNotes>(*note_store_);
    // Soft-delete path (DeleteNote == TrashNote semantics).
    delete_note_ = std::make_unique<application::DeleteNote>(*note_store_, clock_);
    trash_note_ = std::make_unique<application::TrashNote>(*note_store_, clock_);
    // Fallback root is always seeded by migrate(); folder-notes is app default only.
    restore_note_ = std::make_unique<application::RestoreNote>(
        *note_store_, *note_store_, *folder_store_, domain::FolderId{"root"});
    purge_note_ = std::make_unique<application::PurgeNote>(
        *note_store_, *note_store_, attachments_.get());
    list_trashed_notes_ =
        std::make_unique<application::ListTrashedNotes>(*note_store_);
    move_note_ = std::make_unique<application::MoveNote>(
        *note_store_, *note_store_, *folder_store_, clock_);
    toggle_checklist_ = std::make_unique<application::ToggleChecklistItem>(
        *note_store_, *note_store_, clock_);
    attach_file_ = std::make_unique<application::AttachFile>(
        *attachments_, *note_store_, *note_store_, clock_);
    create_folder_ = std::make_unique<application::CreateFolder>(*folder_store_, clock_);
    rename_folder_ =
        std::make_unique<application::RenameFolder>(*folder_store_, *folder_store_, clock_);
    list_folders_ = std::make_unique<application::ListFolders>(*folder_store_);
    delete_folder_ = std::make_unique<application::DeleteFolder>(*folder_store_);

    dispatcher_ = std::make_unique<presentation::UseCaseDispatcher>();
    folder_vm_ = std::make_unique<presentation::FolderTreeViewModel>(
        *list_folders_, *create_folder_, *rename_folder_, *delete_folder_, *dispatcher_);
    note_list_vm_ = std::make_unique<presentation::NoteListViewModel>(
        *list_notes_, *create_note_, *delete_note_, *search_notes_,
        *list_trashed_notes_, *restore_note_, *purge_note_, *move_note_,
        *dispatcher_);
    editor_vm_ = std::make_unique<presentation::EditorViewModel>(
        *load_note_, *save_note_, *dispatcher_, toggle_checklist_.get(),
        attach_file_.get(), attachments_.get());
    exit_gate_ = std::make_unique<presentation::AppExitGate>(editor_vm_.get(),
                                                             dispatcher_.get());
  }

  void register_qml(QQmlApplicationEngine& engine) {
    presentation::registerQmlTypes();
    presentation::exposeToQml(engine, folder_vm_.get(), note_list_vm_.get(),
                              editor_vm_.get(), exit_gate_.get());
  }

  // aboutToQuit safety net only — cannot veto exit. Prefer AppExitGate::requestClose.
  void flush_and_shutdown() {
    if (exit_gate_) {
      (void)exit_gate_->flushBestEffort();
      exit_gate_->completeShutdown();
      return;
    }
    if (editor_vm_) {
      (void)editor_vm_->flushSync();
    }
    if (dispatcher_) {
      dispatcher_->flushAndShutdown();
    }
  }

  // Returns false when flush fails: caller must keep the app alive.
  bool request_close() {
    if (!exit_gate_) {
      return true;
    }
    return exit_gate_->requestClose();
  }

  presentation::FolderTreeViewModel* folders() const { return folder_vm_.get(); }
  presentation::NoteListViewModel* notes() const { return note_list_vm_.get(); }
  presentation::EditorViewModel* editor() const { return editor_vm_.get(); }
  presentation::AppExitGate* exit_gate() const { return exit_gate_.get(); }

  // Use cases for folder/trash UI wiring (owned here; view models may take refs later).
  application::TrashNote* trash_note() const { return trash_note_.get(); }
  application::RestoreNote* restore_note() const { return restore_note_.get(); }
  application::PurgeNote* purge_note() const { return purge_note_.get(); }
  application::ListTrashedNotes* list_trashed_notes() const {
    return list_trashed_notes_.get();
  }
  application::MoveNote* move_note() const { return move_note_.get(); }
  application::ToggleChecklistItem* toggle_checklist() const {
    return toggle_checklist_.get();
  }
  application::AttachFile* attach_file() const { return attach_file_.get(); }

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
  adapters::system::RandomIdSource id_source_;
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
  std::unique_ptr<application::TrashNote> trash_note_;
  std::unique_ptr<application::RestoreNote> restore_note_;
  std::unique_ptr<application::PurgeNote> purge_note_;
  std::unique_ptr<application::ListTrashedNotes> list_trashed_notes_;
  std::unique_ptr<application::MoveNote> move_note_;
  std::unique_ptr<application::ToggleChecklistItem> toggle_checklist_;
  std::unique_ptr<application::AttachFile> attach_file_;
  std::unique_ptr<application::CreateFolder> create_folder_;
  std::unique_ptr<application::RenameFolder> rename_folder_;
  std::unique_ptr<application::ListFolders> list_folders_;
  std::unique_ptr<application::DeleteFolder> delete_folder_;
  std::unique_ptr<presentation::UseCaseDispatcher> dispatcher_;
  std::unique_ptr<presentation::FolderTreeViewModel> folder_vm_;
  std::unique_ptr<presentation::NoteListViewModel> note_list_vm_;
  std::unique_ptr<presentation::EditorViewModel> editor_vm_;
  std::unique_ptr<presentation::AppExitGate> exit_gate_;
};

}  // namespace notes
