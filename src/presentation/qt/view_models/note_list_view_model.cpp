#include "presentation/qt/view_models/note_list_view_model.hpp"

#include "domain/notes/note_content.hpp"
#include "presentation/qt/view_models/error_text.hpp"

#include <QDateTime>

namespace notes::presentation {

NoteListViewModel::NoteListViewModel(application::ListNotes& list_notes,
                                     application::CreateNote& create_note,
                                     application::DeleteNote& delete_note,
                                     application::SearchNotes& search_notes,
                                     UseCaseDispatcher& dispatcher,
                                     QObject* parent)
    : QObject(parent),
      list_notes_(list_notes),
      create_note_(create_note),
      delete_note_(delete_note),
      search_notes_(search_notes),
      dispatcher_(dispatcher),
      model_(new NoteListModel(this)) {}

void NoteListViewModel::setFolderId(const QString& id) {
  if (folder_id_ == id) return;
  folder_id_ = id;
  emit folderIdChanged();
  if (search_query_.trimmed().isEmpty()) refresh();
}

void NoteListViewModel::setSelectedNoteId(const QString& id) {
  if (selected_note_id_ == id) return;
  selected_note_id_ = id;
  emit selectedNoteIdChanged();
  if (!id.isEmpty()) emit openNoteRequested(id);
}

void NoteListViewModel::setSearchQuery(const QString& q) {
  if (search_query_ == q) return;
  search_query_ = q;
  emit searchQueryChanged();
  if (search_query_.trimmed().isEmpty()) {
    setSearching(false);
    refresh();
  } else {
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

void NoteListViewModel::refresh() {
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
        self->model_->setNotes(std::move(result.value()));
      });
}

void NoteListViewModel::createNote() {
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
  domain::NoteSummary sum;
  sum.id = domain::NoteId{noteId.toStdString()};
  sum.folder_id = domain::FolderId{folder_id_.toStdString()};
  sum.title = title.toStdString();
  sum.preview = title.toStdString();
  sum.revision = revision;
  sum.pinned = pinned;
  sum.modified_at_ms = QDateTime::currentMSecsSinceEpoch();
  model_->upsert(sum);
}

}  // namespace notes::presentation
