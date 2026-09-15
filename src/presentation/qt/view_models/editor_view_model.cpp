#include "presentation/qt/view_models/editor_view_model.hpp"

#include "presentation/qt/view_models/error_text.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTextCursor>
#include <QTextDocument>

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

void EditorViewModel::setPlainText(const QString& plain) {
  const auto content = NoteContentDocumentMapper::fromPlain(plain);
  setHtml(NoteContentDocumentMapper::toHtml(content));
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

void EditorViewModel::toggleInlineStyle(int selectionStart, int selectionEnd,
                                        const QString& style) {
  if (note_id_.isEmpty()) return;
  if (selectionStart < 0 || selectionEnd < selectionStart) return;

  QTextDocument doc;
  doc.setHtml(html_);
  if (selectionStart > doc.characterCount() ||
      selectionEnd > doc.characterCount()) {
    return;
  }
  QTextCursor cursor(&doc);
  cursor.setPosition(selectionStart);
  if (selectionEnd > selectionStart) {
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
  }
  if (style == QLatin1String("bold")) {
    NoteContentDocumentMapper::toggleBold(cursor);
  } else if (style == QLatin1String("italic")) {
    NoteContentDocumentMapper::toggleItalic(cursor);
  } else if (style == QLatin1String("underline")) {
    NoteContentDocumentMapper::toggleUnderline(cursor);
  } else {
    return;
  }
  setHtml(doc.toHtml());
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

void EditorViewModel::abandonInFlightUi() {
  ++save_generation_;
  queued_resave_ = false;
  setSaving(false);
}

void EditorViewModel::clearEditorState() {
  note_id_.clear();
  folder_id_.clear();
  html_.clear();
  plain_.clear();
  last_saved_html_.clear();
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

void EditorViewModel::applySaveSuccess(const application::SaveNote::Outcome& out,
                                       const QString& html_snapshot) {
  note_id_ = QString::fromStdString(out.saved.id.value());
  folder_id_ = QString::fromStdString(out.saved.folder_id.value());
  revision_ = out.saved.revision;
  pinned_ = out.saved.pinned;
  created_at_ms_ = out.saved.created_at_ms;
  emit noteIdChanged();
  emit revisionChanged();
  emit pinnedChanged();
  if (out.kept_both) {
    emit keepBothNotice(QStringLiteral("Edit conflict: kept both copies."));
  }
  if (html_ == html_snapshot) {
    last_saved_html_ = html_snapshot;
    setDirty(false);
  } else {
    setDirty(true);
    scheduleDebouncedSave();
  }
  const QString title = QString::fromStdString(
      out.saved.title.empty() ? out.saved.content.plain_text()
                              : out.saved.title);
  emit saved(QString::fromStdString(out.saved.id.value()), title,
             out.saved.revision, out.saved.pinned);
}

void EditorViewModel::openNote(const QString& noteId) {
  if (noteId.isEmpty()) {
    closeNote();
    return;
  }
  if (!note_id_.isEmpty() && note_id_ != noteId && (dirty_ || saving_)) {
    if (!flushPendingSavesBlocking()) {
      return;
    }
  }
  idle_timer_->stop();
  max_timer_->stop();
  setLoading(true);
  setError({});
  const domain::NoteId id{noteId.toStdString()};
  const int gen = ++load_generation_;
  abandonInFlightUi();
  QPointer<EditorViewModel> self(this);
  dispatcher_.postResult<application::Result<domain::Note>>(
      [this, id]() { return load_note_.execute(id); }, this,
      [self, gen, noteId](application::Result<domain::Note> result) {
        if (!self) return;
        if (gen != self->load_generation_) {
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
  if (!note_id_.isEmpty() && (dirty_ || saving_)) {
    if (!flushPendingSavesBlocking()) {
      return;
    }
  }
  ++load_generation_;
  abandonInFlightUi();
  clearEditorState();
}

void EditorViewModel::saveNow() {
  idle_timer_->stop();
  max_timer_->stop();
  performSave(true);
}

void EditorViewModel::performSave(bool /*from_max_timer*/) {
  if (note_id_.isEmpty() || !dirty_) return;
  if (saving_) {
    queued_resave_ = true;
    return;
  }
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
        if (gen != self->save_generation_ ||
            self->note_id_ != expected_note_id) {
          return;
        }
        self->setSaving(false);
        if (!result) {
          self->setError(errorText(result.error()));
          self->queued_resave_ = false;
          return;
        }
        self->applySaveSuccess(result.value(), html_snapshot);
        if (self->queued_resave_) {
          self->queued_resave_ = false;
          if (self->dirty_) {
            self->performSave(true);
          }
        }
      });
}

std::optional<application::SaveNote::Request>
EditorViewModel::pendingSaveRequest() const {
  if (note_id_.isEmpty() || (!dirty_ && !saving_)) return std::nullopt;
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
          self->setSaving(false);
        } else if (self && gen == self->save_generation_ && !result) {
          self->setError(errorText(result.error()));
          self->setSaving(false);
        }
        if (done) done();
      });
}

bool EditorViewModel::flushPendingSavesBlocking() {
  idle_timer_->stop();
  max_timer_->stop();
  if (note_id_.isEmpty()) {
    setSaving(false);
    queued_resave_ = false;
    return true;
  }

  if (saving_) {
    dispatcher_.runBlocking([] {});
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1000);
  }

  if (!dirty_) {
    queued_resave_ = false;
    setSaving(false);
    return error_.isEmpty();
  }

  application::SaveNote::Request req;
  req.note = noteSnapshot();
  req.allow_keep_both = true;
  const QString html_snapshot = html_;
  const QString expected_note_id = note_id_;
  ++save_generation_;
  queued_resave_ = false;
  setSaving(true);
  setError({});

  application::Result<application::SaveNote::Outcome> result =
      application::Result<application::SaveNote::Outcome>::fail(
          {application::ErrorKind::StorageFailure, "flush not executed"});
  dispatcher_.runBlocking([this, &result, req = std::move(req)]() mutable {
    result = save_note_.execute(std::move(req));
  });
  setSaving(false);

  if (note_id_ != expected_note_id) {
    return false;
  }
  if (!result) {
    setError(errorText(result.error()));
    return false;
  }
  applySaveSuccess(result.value(), html_snapshot);
  return true;
}

bool EditorViewModel::flushSync() { return flushPendingSavesBlocking(); }

}  // namespace notes::presentation
