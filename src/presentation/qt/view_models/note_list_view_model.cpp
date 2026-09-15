#include "presentation/qt/view_models/note_list_view_model.hpp"

#include "domain/notes/note_content.hpp"
#include "presentation/qt/view_models/error_text.hpp"

#include <QDateTime>

namespace notes::presentation {

NoteListViewModel::NoteListViewModel(
    application::ListNotes& list_notes, application::CreateNote& create_note,
    application::DeleteNote& delete_note,
    application::SearchNotes& search_notes,
    application::ListTrashedNotes& list_trashed,
    application::RestoreNote& restore_note, application::PurgeNote& purge_note,
    application::MoveNote& move_note, UseCaseDispatcher& dispatcher,
    QObject* parent)
    : QObject(parent),
      list_notes_(list_notes),
      create_note_(create_note),
      delete_note_(delete_note),
      search_notes_(search_notes),
      list_trashed_(list_trashed),
      restore_note_(restore_note),
      purge_note_(purge_note),
      move_note_(move_note),
      dispatcher_(dispatcher),
      model_(new NoteListModel(this)) {}

void NoteListViewModel::setFolderId(const QString& id) {
  if (folder_id_ == id && !showing_trash_) return;
  const bool folder_changed = folder_id_ != id;
  folder_id_ = id;
  emit folderIdChanged();
  if (showing_trash_) {
    setShowingTrash(false);
  }
  if (folder_changed) {
    clearSelectionQuiet();
  }
  if (search_query_.trimmed().isEmpty()) refresh();
}

void NoteListViewModel::setSelectedNoteId(const QString& id) {
  if (selected_note_id_ == id) return;
  selected_note_id_ = id;
  emit selectedNoteIdChanged();
  if (!id.isEmpty()) emit openNoteRequested(id);
}

void NoteListViewModel::clearSelectionQuiet() {
  if (selected_note_id_.isEmpty()) return;
  selected_note_id_.clear();
  emit selectedNoteIdChanged();
}

void NoteListViewModel::pruneSelectionToModel() {
  if (selected_note_id_.isEmpty()) return;
  const domain::NoteId id{selected_note_id_.toStdString()};
  if (!model_->contains(id)) {
    clearSelectionQuiet();
  }
}

void NoteListViewModel::setSearchQuery(const QString& q) {
  if (search_query_ == q) return;
  search_query_ = q;
  emit searchQueryChanged();
  if (search_query_.trimmed().isEmpty()) {
    setSearching(false);
    refresh();
  } else {
    setShowingTrash(false);
    runSearch();
  }
}

void NoteListViewModel::setBusy(bool v) {
  if (busy_ == v) return;
  busy_ = v;
  emit busyChanged();
}
void NoteListViewModel::setError(QString e) {
  if (error_ == e) return;
  error_ = std::move(e);
  emit errorStringChanged();
}
void NoteListViewModel::setSearching(bool v) {
  if (searching_ == v) return;
  searching_ = v;
  emit searchingChanged();
}
void NoteListViewModel::setShowingTrash(bool v) {
  if (showing_trash_ == v) return;
  showing_trash_ = v;
  emit showingTrashChanged();
}

void NoteListViewModel::showTrash() {
  // Leaving folder/search context: drop selection so editor cannot keep a
  // live note open while the list shows only trash (or vice versa).
  clearSelectionQuiet();
  if (!search_query_.isEmpty()) {
    search_query_.clear();
    emit searchQueryChanged();
    setSearching(false);
  }
  setShowingTrash(true);
  runListTrashed();
}

void NoteListViewModel::hideTrash() {
  clearSelectionQuiet();
  setShowingTrash(false);
  refresh();
}

void NoteListViewModel::runListTrashed() {
  setBusy(true);
  setError({});
  const int gen = ++search_generation_;
  QPointer<NoteListViewModel> self(this);
  dispatcher_.postResult<application::Result<std::vector<domain::NoteSummary>>>(
      [this]() { return list_trashed_.execute(); }, this,
      [self, gen](application::Result<std::vector<domain::NoteSummary>> result) {
        if (!self || gen != self->search_generation_) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        self->model_->setNotes(std::move(result.value()));
        self->pruneSelectionToModel();
      });
}

void NoteListViewModel::refresh() {
  if (showing_trash_) {
    runListTrashed();
    return;
  }
  if (!search_query_.trimmed().isEmpty()) {
    runSearch();
    return;
  }
  if (folder_id_.isEmpty()) {
    model_->setNotes({});
    return;
  }
  setBusy(true);
  setError({});
  const auto folder = domain::FolderId{folder_id_.toStdString()};
  const int gen = ++search_generation_;
  QPointer<NoteListViewModel> self(this);
  dispatcher_.postResult<application::Result<std::vector<domain::NoteSummary>>>(
      [this, folder]() { return list_notes_.execute(folder); }, this,
      [self, gen](application::Result<std::vector<domain::NoteSummary>> result) {
        if (!self || gen != self->search_generation_) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        self->model_->setNotes(std::move(result.value()));
        self->pruneSelectionToModel();
      });
}

void NoteListViewModel::runSearch() {
  setSearching(true);
  setBusy(true);
  setError({});
  const int gen = ++search_generation_;
  const std::string query = search_query_.trimmed().toStdString();
  QPointer<NoteListViewModel> self(this);
  dispatcher_.postResult<application::Result<std::vector<domain::NoteSummary>>>(
      [this, query]() { return search_notes_.execute(query); }, this,
      [self, gen](application::Result<std::vector<domain::NoteSummary>> result) {
        if (!self || gen != self->search_generation_) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        // Store search already excludes trashed_at>0.
        self->model_->setNotes(std::move(result.value()));
        self->pruneSelectionToModel();
      });
}

void NoteListViewModel::createNote() {
  if (showing_trash_) {
    setError(QStringLiteral("Leave trash before creating a note"));
    return;
  }
  if (folder_id_.isEmpty()) {
    setError(QStringLiteral("Select a folder before creating a note"));
    return;
  }
  application::CreateNote::Request req;
  req.folder_id = domain::FolderId{folder_id_.toStdString()};
  req.title = {};
  req.content = domain::NoteContent::from_plain_text("");
  setBusy(true);
  setError({});
  QPointer<NoteListViewModel> self(this);
  dispatcher_.postResult<application::Result<domain::Note>>(
      [this, req = std::move(req)]() mutable {
        return create_note_.execute(std::move(req));
      },
      this, [self](application::Result<domain::Note> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        const auto& note = result.value();
        domain::NoteSummary sum;
        sum.id = note.id;
        sum.folder_id = note.folder_id;
        sum.title = note.title;
        sum.preview = note.content.plain_text();
        sum.modified_at_ms = note.modified_at_ms;
        sum.revision = note.revision;
        sum.pinned = note.pinned;
        self->model_->upsert(sum);
        const QString id = QString::fromStdString(note.id.value());
        self->setSelectedNoteId(id);
        emit self->noteCreated(id);
      });
}

void NoteListViewModel::deleteNote(const QString& noteId) {
  trashNote(noteId);
}

void NoteListViewModel::trashNote(const QString& noteId) {
  if (noteId.isEmpty()) return;
  const domain::NoteId id{noteId.toStdString()};
  setBusy(true);
  setError({});
  QPointer<NoteListViewModel> self(this);
  dispatcher_.postResult<application::Result<void>>(
      [this, id]() { return delete_note_.execute(id); }, this,
      [self, noteId](application::Result<void> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        self->model_->removeById(domain::NoteId{noteId.toStdString()});
        if (self->selected_note_id_ == noteId) {
          self->setSelectedNoteId({});
        }
        emit self->noteDeleted(noteId);
        if (self->showing_trash_) self->refresh();
      });
}

void NoteListViewModel::restoreNote(const QString& noteId) {
  if (noteId.isEmpty()) return;
  const domain::NoteId id{noteId.toStdString()};
  setBusy(true);
  setError({});
  QPointer<NoteListViewModel> self(this);
  dispatcher_.postResult<application::Result<domain::Note>>(
      [this, id]() { return restore_note_.execute(id); }, this,
      [self, noteId](application::Result<domain::Note> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        self->model_->removeById(domain::NoteId{noteId.toStdString()});
        if (self->selected_note_id_ == noteId) {
          self->setSelectedNoteId({});
        }
        emit self->noteRestored(noteId);
        self->refresh();
      });
}

void NoteListViewModel::purgeNote(const QString& noteId) {
  if (noteId.isEmpty()) return;
  const domain::NoteId id{noteId.toStdString()};
  setBusy(true);
  setError({});
  QPointer<NoteListViewModel> self(this);
  dispatcher_.postResult<application::Result<void>>(
      [this, id]() { return purge_note_.execute(id); }, this,
      [self, noteId](application::Result<void> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        self->model_->removeById(domain::NoteId{noteId.toStdString()});
        if (self->selected_note_id_ == noteId) {
          self->setSelectedNoteId({});
        }
        emit self->notePurged(noteId);
      });
}

void NoteListViewModel::moveNote(const QString& noteId,
                                 const QString& targetFolderId) {
  if (noteId.isEmpty() || targetFolderId.isEmpty()) return;
  application::MoveNote::Request req;
  req.note_id = domain::NoteId{noteId.toStdString()};
  req.target_folder_id = domain::FolderId{targetFolderId.toStdString()};
  setBusy(true);
  setError({});
  QPointer<NoteListViewModel> self(this);
  dispatcher_.postResult<application::Result<domain::Note>>(
      [this, req = std::move(req)]() mutable {
        return move_note_.execute(std::move(req));
      },
      this, [self, noteId, targetFolderId](application::Result<domain::Note> result) {
        if (!self) return;
        self->setBusy(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        // Drop from current folder list if we moved away.
        if (!self->showing_trash_ && self->folder_id_ != targetFolderId) {
          self->model_->removeById(domain::NoteId{noteId.toStdString()});
          if (self->selected_note_id_ == noteId) {
            self->setSelectedNoteId({});
          }
        } else {
          self->refresh();
        }
        emit self->noteMoved(noteId, targetFolderId);
      });
}

void NoteListViewModel::selectNoteAt(int row) {
  const auto id = model_->noteIdAt(row);
  if (id.empty()) return;
  setSelectedNoteId(QString::fromStdString(id.value()));
}

void NoteListViewModel::applySummaryTitle(const QString& noteId,
                                          const QString& title, qint64 revision,
                                          bool pinned) {
  // Only touch rows already in the visible list (folder/search/trash). Avoid
  // inserting a note into the wrong context after move/trash.
  domain::NoteSummary sum;
  sum.id = domain::NoteId{noteId.toStdString()};
  sum.folder_id = domain::FolderId{folder_id_.toStdString()};
  sum.title = title.toStdString();
  sum.preview = title.toStdString();
  sum.revision = revision;
  sum.pinned = pinned;
  sum.modified_at_ms = QDateTime::currentMSecsSinceEpoch();
  model_->updateIfPresent(sum);
}

}  // namespace notes::presentation
