#include "presentation/qt/view_models/editor_view_model.hpp"

#include "presentation/qt/view_models/error_text.hpp"

namespace notes::presentation {

EditorViewModel::EditorViewModel(application::LoadNote& load_note,
                                 application::SaveNote& save_note,
                                 UseCaseDispatcher& dispatcher, QObject* parent)
    : QObject(parent),
      load_note_(load_note),
      save_note_(save_note),
      dispatcher_(dispatcher),
      idle_timer_(new QTimer(this)),
      max_timer_(new QTimer(this)) {
  idle_timer_->setSingleShot(true);
  idle_timer_->setInterval(2000);
  connect(idle_timer_, &QTimer::timeout, this, [this]() { performSave(false); });
  max_timer_->setSingleShot(true);
  max_timer_->setInterval(10000);
  connect(max_timer_, &QTimer::timeout, this, [this]() { performSave(true); });
}

QString EditorViewModel::saveState() const {
  if (loading_) return QStringLiteral("loading");
  if (saving_) return QStringLiteral("saving");
  if (!error_.isEmpty()) return QStringLiteral("error");
  if (dirty_) return QStringLiteral("dirty");
  if (note_id_.isEmpty()) return QStringLiteral("empty");
  return QStringLiteral("saved");
}

void EditorViewModel::emitSaveState() { emit saveStateChanged(); }

void EditorViewModel::setDirty(bool v) {
  if (dirty_ == v) return;
  dirty_ = v;
  emit dirtyChanged();
  emitSaveState();
}
void EditorViewModel::setSaving(bool v) {
  if (saving_ == v) return;
  saving_ = v;
  emit savingChanged();
  emitSaveState();
}
void EditorViewModel::setLoading(bool v) {
  if (loading_ == v) return;
  loading_ = v;
  emit loadingChanged();
  emitSaveState();
}
void EditorViewModel::setError(QString e) {
  if (error_ == e) return;
  error_ = std::move(e);
  emit errorStringChanged();
  emitSaveState();
}

void EditorViewModel::setPinned(bool pinned) {
  if (pinned_ == pinned) return;
  pinned_ = pinned;
  emit pinnedChanged();
  setDirty(true);
  scheduleDebouncedSave();
}

void EditorViewModel::markUndoRedo(bool canUndo, bool canRedo) {
  if (can_undo_ == canUndo && can_redo_ == canRedo) return;
  can_undo_ = canUndo;
  can_redo_ = canRedo;
  emit undoStateChanged();
}

void EditorViewModel::setHtml(const QString& html) {
  if (applying_load_) {
    html_ = html;
    plain_ = NoteContentDocumentMapper::toPlain(
        NoteContentDocumentMapper::fromHtml(html));
    emit htmlChanged();
    emit plainTextChanged();
    return;
  }
  if (html_ == html) return;
  html_ = html;
  plain_ = NoteContentDocumentMapper::toPlain(
      NoteContentDocumentMapper::fromHtml(html));
  emit htmlChanged();
  emit plainTextChanged();
  if (note_id_.isEmpty()) return;
  if (html_ == last_saved_html_) {
    setDirty(false);
    idle_timer_->stop();
    return;
  }
  setDirty(true);
  setError({});
  scheduleDebouncedSave();
}

void EditorViewModel::scheduleDebouncedSave() {
  idle_timer_->start();
  if (!max_timer_->isActive()) max_timer_->start();
}

domain::NoteContent EditorViewModel::contentFromEditor() const {
  return NoteContentDocumentMapper::fromHtml(html_);
}

domain::Note EditorViewModel::noteSnapshot() const {
  domain::Note note;
  note.id = domain::NoteId{note_id_.toStdString()};
  note.folder_id = domain::FolderId{folder_id_.toStdString()};
  note.title = title_from_plain(plain_);
  note.content = contentFromEditor();
  note.created_at_ms = created_at_ms_;
  note.modified_at_ms = 0;
  note.revision = revision_;
  note.pinned = pinned_;
  return note;
}

std::string EditorViewModel::title_from_plain(const QString& plain) {
  const QString trimmed = plain.trimmed();
  if (trimmed.isEmpty()) return {};
  return trimmed.split(QLatin1Char('\n')).value(0).left(120).toStdString();
}

void EditorViewModel::openNote(const QString& noteId) {
  if (noteId.isEmpty()) {
    closeNote();
    return;
  }
  // Flush dirty previous note without applying completion onto the new note.
  if (dirty_ && !note_id_.isEmpty() && note_id_ != noteId) {
    performSave(true);
  }
  idle_timer_->stop();
  max_timer_->stop();
  setLoading(true);
  setError({});
  const domain::NoteId id{noteId.toStdString()};
  const int gen = ++load_generation_;
  // Bump save_generation so in-flight saves for other notes cannot clear dirty
  // or overwrite revision on this editor instance after switch.
  ++save_generation_;
  QPointer<EditorViewModel> self(this);
  dispatcher_.postResult<application::Result<domain::Note>>(
      [this, id]() { return load_note_.execute(id); }, this,
      [self, gen, noteId](application::Result<domain::Note> result) {
        if (!self) return;
        if (gen != self->load_generation_) {
          // Stale load — ignore completely.
          return;
        }
        self->setLoading(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        const domain::Note& note = result.value();
        if (QString::fromStdString(note.id.value()) != noteId) {
          return;
        }
        self->note_id_ = QString::fromStdString(note.id.value());
        self->folder_id_ = QString::fromStdString(note.folder_id.value());
        self->revision_ = note.revision;
        self->pinned_ = note.pinned;
        self->created_at_ms_ = note.created_at_ms;
        self->applying_load_ = true;
        self->html_ = NoteContentDocumentMapper::toHtml(note.content);
        self->plain_ = NoteContentDocumentMapper::toPlain(note.content);
        self->last_saved_html_ = self->html_;
        self->applying_load_ = false;
        self->setDirty(false);
        emit self->noteIdChanged();
        emit self->revisionChanged();
        emit self->pinnedChanged();
        emit self->htmlChanged();
        emit self->plainTextChanged();
        self->emitSaveState();
      });
}

void EditorViewModel::closeNote() {
  idle_timer_->stop();
  max_timer_->stop();
  if (dirty_) performSave(true);
  ++load_generation_;
  ++save_generation_;
  note_id_.clear();
  folder_id_.clear();
  html_.clear();
  plain_.clear();
  revision_ = 0;
  created_at_ms_ = 0;
  pinned_ = false;
  setDirty(false);
  setError({});
  emit noteIdChanged();
  emit htmlChanged();
  emit plainTextChanged();
  emit revisionChanged();
  emit pinnedChanged();
  emitSaveState();
}

void EditorViewModel::saveNow() {
  idle_timer_->stop();
  max_timer_->stop();
  performSave(true);
}

void EditorViewModel::performSave(bool /*from_max_timer*/) {
  if (note_id_.isEmpty() || !dirty_ || saving_) return;
  idle_timer_->stop();
  max_timer_->stop();
  setSaving(true);
  setError({});

  application::SaveNote::Request req;
  req.note = noteSnapshot();
  req.allow_keep_both = true;
  const QString html_snapshot = html_;
  const QString expected_note_id = note_id_;
  const int gen = ++save_generation_;
  QPointer<EditorViewModel> self(this);
  dispatcher_.postResult<application::Result<application::SaveNote::Outcome>>(
      [this, req = std::move(req)]() mutable {
        return save_note_.execute(std::move(req));
      },
      this,
      [self, gen, html_snapshot, expected_note_id](
          application::Result<application::SaveNote::Outcome> result) {
        if (!self) return;
        // Stale save: note switched or a newer save was started.
        if (gen != self->save_generation_ ||
            self->note_id_ != expected_note_id) {
          // Still clear saving only if this completion was the active one.
          // If gen mismatches, a newer save owns saving_ state.
          if (gen == self->save_generation_) {
            self->setSaving(false);
          }
          return;
        }
        self->setSaving(false);
        if (!result) {
          // Keep dirty — no silent data loss.
          self->setError(errorText(result.error()));
          return;
        }
        const auto& out = result.value();
        // Apply only if still on the same editor note.
        self->note_id_ = QString::fromStdString(out.saved.id.value());
        self->folder_id_ = QString::fromStdString(out.saved.folder_id.value());
        self->revision_ = out.saved.revision;
        self->pinned_ = out.saved.pinned;
        self->created_at_ms_ = out.saved.created_at_ms;
        emit self->noteIdChanged();
        emit self->revisionChanged();
        emit self->pinnedChanged();
        if (out.kept_both) {
          emit self->keepBothNotice(
              QStringLiteral("Edit conflict: kept both copies."));
        }
        if (self->html_ == html_snapshot) {
          self->last_saved_html_ = html_snapshot;
          self->setDirty(false);
        } else {
          self->setDirty(true);
          self->scheduleDebouncedSave();
        }
        const QString title = QString::fromStdString(
            out.saved.title.empty() ? out.saved.content.plain_text()
                                    : out.saved.title);
        emit self->saved(QString::fromStdString(out.saved.id.value()), title,
                         out.saved.revision, out.saved.pinned);
      });
}

std::optional<application::SaveNote::Request>
EditorViewModel::pendingSaveRequest() const {
  if (note_id_.isEmpty() || !dirty_) return std::nullopt;
  application::SaveNote::Request req;
  req.note = noteSnapshot();
  req.allow_keep_both = true;
  return req;
}

void EditorViewModel::flushDirtySyncRequest(std::function<void()> done) {
  idle_timer_->stop();
  max_timer_->stop();
  if (note_id_.isEmpty() || !dirty_) {
    if (done) done();
    return;
  }
  application::SaveNote::Request req;
  req.note = noteSnapshot();
  req.allow_keep_both = true;
  const int gen = ++save_generation_;
  const QString expected = note_id_;
  QPointer<EditorViewModel> self(this);
  dispatcher_.postResult<application::Result<application::SaveNote::Outcome>>(
      [this, req = std::move(req)]() mutable {
        return save_note_.execute(std::move(req));
      },
      this,
      [self, done = std::move(done), gen, expected](
          application::Result<application::SaveNote::Outcome> result) {
        if (self && gen == self->save_generation_ &&
            self->note_id_ == expected && result) {
          self->revision_ = result.value().saved.revision;
          self->setDirty(false);
        } else if (self && gen == self->save_generation_ && !result) {
          self->setError(errorText(result.error()));
        }
        if (done) done();
      });
}

void EditorViewModel::flushSync() {
  idle_timer_->stop();
  max_timer_->stop();
  auto req = pendingSaveRequest();
  if (!req) {
    return;
  }
  // Block until the pending save finishes on the IO strand (aboutToQuit only).
  // Does not stop the dispatcher — composition_root calls shutdown() after.
  const auto request = *req;
  dispatcher_.runBlocking([this, request]() mutable {
    (void)save_note_.execute(std::move(request));
  });
  dirty_ = false;
}

}  // namespace notes::presentation
