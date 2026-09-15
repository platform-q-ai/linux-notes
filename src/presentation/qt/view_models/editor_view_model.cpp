#include "presentation/qt/view_models/editor_view_model.hpp"

#include "presentation/qt/view_models/error_text.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <algorithm>
#include <variant>
#include <vector>

namespace notes::presentation {
namespace {

constexpr qint64 kMaxAttachBytes = 25 * 1024 * 1024;  // 25 MiB local safety cap

QString guess_mime(const QString& path) {
  const QString lower = path.toLower();
  if (lower.endsWith(QLatin1String(".png"))) return QStringLiteral("image/png");
  if (lower.endsWith(QLatin1String(".jpg")) ||
      lower.endsWith(QLatin1String(".jpeg"))) {
    return QStringLiteral("image/jpeg");
  }
  if (lower.endsWith(QLatin1String(".gif"))) return QStringLiteral("image/gif");
  if (lower.endsWith(QLatin1String(".webp"))) {
    return QStringLiteral("image/webp");
  }
  if (lower.endsWith(QLatin1String(".pdf"))) {
    return QStringLiteral("application/pdf");
  }
  if (lower.endsWith(QLatin1String(".txt")) ||
      lower.endsWith(QLatin1String(".md"))) {
    return QStringLiteral("text/plain");
  }
  return QStringLiteral("application/octet-stream");
}

}  // namespace

EditorViewModel::EditorViewModel(
    application::LoadNote& load_note, application::SaveNote& save_note,
    UseCaseDispatcher& dispatcher,
    application::ToggleChecklistItem* toggle_checklist,
    application::AttachFile* attach_file,
    application::AttachmentStore* attachment_store, QObject* parent)
    : QObject(parent),
      load_note_(load_note),
      save_note_(save_note),
      dispatcher_(dispatcher),
      toggle_checklist_(toggle_checklist),
      attach_file_(attach_file),
      attachment_store_(attachment_store),
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
    auto content = NoteContentDocumentMapper::fromHtml(html);
    cacheContent(content);
    refreshAttachmentLists(content);
    plain_ = NoteContentDocumentMapper::toPlain(content);
    emit htmlChanged();
    emit plainTextChanged();
    return;
  }
  if (html_ == html) return;
  html_ = html;
  auto content = NoteContentDocumentMapper::fromHtml(html);
  cacheContent(content);
  refreshAttachmentLists(content);
  plain_ = NoteContentDocumentMapper::toPlain(content);
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

bool EditorViewModel::is_safe_local_file_path(const QString& path,
                                             QString* reason) {
  if (path.isEmpty()) {
    if (reason) *reason = QStringLiteral("empty path");
    return false;
  }
  // Reject URLs / remote schemes — local files only.
  const QUrl url(path);
  if (url.isValid() && !url.scheme().isEmpty() &&
      url.scheme() != QLatin1String("file")) {
    if (reason) *reason = QStringLiteral("only local files allowed");
    return false;
  }
  QString local = path;
  if (url.isValid() && url.isLocalFile()) {
    local = url.toLocalFile();
  }
  if (local.contains(QLatin1Char('\0'))) {
    if (reason) *reason = QStringLiteral("invalid path");
    return false;
  }
  const QFileInfo info(local);
  if (!info.exists() || !info.isFile()) {
    if (reason) *reason = QStringLiteral("file not found");
    return false;
  }
  // No following into non-regular special files.
  if (info.isSymLink()) {
    const QFileInfo target(info.symLinkTarget());
    if (!target.exists() || !target.isFile()) {
      if (reason) *reason = QStringLiteral("unsafe symlink");
      return false;
    }
  }
  if (info.isExecutable() && !info.suffix().isEmpty() &&
      (info.suffix() == QLatin1String("so") ||
       info.suffix() == QLatin1String("sh") ||
       info.suffix() == QLatin1String("bin"))) {
    // Still allow attach of scripts as inert blobs — we never execute them.
  }
  if (info.size() <= 0) {
    // empty files are ok
  } else if (info.size() > kMaxAttachBytes) {
    if (reason) *reason = QStringLiteral("file too large");
    return false;
  }
  return true;
}

void EditorViewModel::applyLoadedNote(const domain::Note& note, bool mark_clean) {
  note_id_ = QString::fromStdString(note.id.value());
  folder_id_ = QString::fromStdString(note.folder_id.value());
  revision_ = note.revision;
  pinned_ = note.pinned;
  created_at_ms_ = note.created_at_ms;
  applying_load_ = true;
  cacheContent(note.content);
  refreshAttachmentLists(note.content);
  html_ = NoteContentDocumentMapper::toHtml(note.content);
  plain_ = NoteContentDocumentMapper::toPlain(note.content);
  if (mark_clean) {
    last_saved_html_ = html_;
  }
  applying_load_ = false;
  emit noteIdChanged();
  emit revisionChanged();
  emit pinnedChanged();
  emit htmlChanged();
  emit plainTextChanged();
  if (mark_clean) {
    setDirty(false);
  } else {
    setDirty(true);
  }
  emitSaveState();
  emit attachmentChanged();
}

void EditorViewModel::insertChecklist() {
  if (note_id_.isEmpty()) return;
  auto content = contentFromEditor();
  auto blocks = content.blocks();
  blocks.emplace_back(domain::ChecklistBlock{
      std::vector<domain::ChecklistItem>{domain::ChecklistItem{false, ""}}});
  content = domain::NoteContent{std::move(blocks)};
  setHtml(NoteContentDocumentMapper::toHtml(content));
}

bool EditorViewModel::toggleChecklistAtPlainOffset(int plainOffset) {
  if (note_id_.isEmpty() || plainOffset < 0) return false;
  const auto content = contentFromEditor();
  int pos = 0;
  int block_index = 0;
  for (const auto& block : content.blocks()) {
    if (const auto* check = std::get_if<domain::ChecklistBlock>(&block)) {
      for (std::size_t i = 0; i < check->items().size(); ++i) {
        const QString line =
            (check->items()[i].done ? QStringLiteral("[x] ")
                                    : QStringLiteral("[ ] ")) +
            QString::fromStdString(check->items()[i].text);
        const int line_start = pos;
        const int line_end = pos + line.size();
        // Marker is first 3–4 chars; allow click anywhere on the line.
        if (plainOffset >= line_start && plainOffset <= line_end) {
          toggleChecklistItem(block_index, static_cast<int>(i));
          return true;
        }
        pos = line_end + 1;  // newline between plain_text lines
      }
      ++block_index;
      continue;
    }
    if (const auto* para = std::get_if<domain::ParagraphBlock>(&block)) {
      std::string t;
      for (const auto& s : para->spans) t += s.text;
      pos += static_cast<int>(t.size()) + 1;
      ++block_index;
      continue;
    }
    if (const auto* att = std::get_if<domain::AttachmentRefBlock>(&block)) {
      const std::string label =
          att->display_name.empty() ? att->attachment_id.value()
                                    : att->display_name;
      pos += static_cast<int>(label.size()) + 1;
      ++block_index;
      continue;
    }
    ++block_index;
  }
  return false;
}

void EditorViewModel::refreshAttachmentLists(const domain::NoteContent& content) {
  attachment_ids_.clear();
  attachment_names_.clear();
  for (const auto& b : content.blocks()) {
    if (const auto* a = std::get_if<domain::AttachmentRefBlock>(&b)) {
      attachment_ids_.push_back(QString::fromStdString(a->attachment_id.value()));
      attachment_names_.push_back(QString::fromStdString(
          a->display_name.empty() ? a->attachment_id.value()
                                  : a->display_name));
    }
  }
}

QStringList EditorViewModel::attachmentIds() const { return attachment_ids_; }

QStringList EditorViewModel::attachmentNames() const {
  return attachment_names_;
}

void EditorViewModel::toggleChecklistItem(int blockIndex, int itemIndex) {
  if (note_id_.isEmpty()) {
    setError(QStringLiteral("Checklist toggle unavailable"));
    return;
  }
  if (blockIndex < 0 || itemIndex < 0) {
    setError(QStringLiteral("Invalid checklist index"));
    return;
  }

  // Prefer use-case path (CAS revision) when wired by CompositionRoot.
  if (toggle_checklist_) {
    if (dirty_ || saving_) {
      if (!flushPendingSavesBlocking()) {
        return;
      }
    }
    application::ToggleChecklistItem::Request req;
    req.note_id = domain::NoteId{note_id_.toStdString()};
    req.block_index = static_cast<std::size_t>(blockIndex);
    req.item_index = static_cast<std::size_t>(itemIndex);
    req.base_revision = revision_;
    setSaving(true);
    setError({});
    QPointer<EditorViewModel> self(this);
    const QString expected = note_id_;
    const int abandon_at_post = structured_op_generation_;
    dispatcher_.postResult<application::Result<domain::Note>>(
        [this, req]() { return toggle_checklist_->execute(req); }, this,
        [self, expected, abandon_at_post](
            application::Result<domain::Note> result) {
          if (!self) return;
          // Only drop if note switched or openNote/close abandoned ops.
          if (self->note_id_ != expected ||
              self->structured_op_generation_ != abandon_at_post) {
            return;
          }
          self->setSaving(false);
          if (!result) {
            self->setError(errorText(result.error()));
            return;
          }
          self->applyLoadedNote(result.value(), true);
          const QString title = QString::fromStdString(
              result.value().title.empty()
                  ? result.value().content.plain_text()
                  : result.value().title);
          emit self->saved(QString::fromStdString(result.value().id.value()),
                           title, result.value().revision,
                           result.value().pinned);
        });
    return;
  }

  // Local fallback: mutate editor content + dirty save (tests / unwired root).
  auto blocks = contentFromEditor().blocks();
  if (static_cast<std::size_t>(blockIndex) >= blocks.size()) {
    setError(QStringLiteral("Invalid checklist block"));
    return;
  }
  auto* checklist =
      std::get_if<domain::ChecklistBlock>(&blocks[static_cast<std::size_t>(blockIndex)]);
  if (!checklist) {
    setError(QStringLiteral("Not a checklist block"));
    return;
  }
  blocks[static_cast<std::size_t>(blockIndex)] =
      checklist->with_toggled(static_cast<std::size_t>(itemIndex));
  setHtml(NoteContentDocumentMapper::toHtml(
      domain::NoteContent{std::move(blocks)}));
}

void EditorViewModel::attachLocalFile(const QString& localPath) {
  if (note_id_.isEmpty() || !attach_file_) {
    setError(QStringLiteral("Attach unavailable"));
    return;
  }
  QString reason;
  if (!is_safe_local_file_path(localPath, &reason)) {
    setError(QStringLiteral("Unsafe or invalid path: %1").arg(reason));
    return;
  }
  QString path = localPath;
  const QUrl url(localPath);
  if (url.isValid() && url.isLocalFile()) {
    path = url.toLocalFile();
  }
  const QFileInfo info(path);
  QFile file(info.absoluteFilePath());
  if (!file.open(QIODevice::ReadOnly)) {
    setError(QStringLiteral("Cannot read file"));
    return;
  }
  if (file.size() > kMaxAttachBytes) {
    setError(QStringLiteral("File too large"));
    return;
  }
  const QByteArray bytes = file.readAll();
  file.close();

  if (dirty_ || saving_) {
    if (!flushPendingSavesBlocking()) {
      return;
    }
  }

  application::AttachFile::Request req;
  req.note_id = domain::NoteId{note_id_.toStdString()};
  req.file_name = info.fileName().toStdString();
  req.mime_type = guess_mime(info.fileName()).toStdString();
  req.bytes.assign(bytes.begin(), bytes.end());
  req.base_revision = revision_;

  setSaving(true);
  setError({});
  QPointer<EditorViewModel> self(this);
  const QString expected = note_id_;
  const int abandon_at_post = structured_op_generation_;
  dispatcher_.postResult<application::Result<application::AttachFile::Outcome>>(
      [this, req = std::move(req)]() mutable {
        return attach_file_->execute(std::move(req));
      },
      this,
      [self, expected, abandon_at_post](
          application::Result<application::AttachFile::Outcome> result) {
        if (!self) return;
        if (self->note_id_ != expected ||
            self->structured_op_generation_ != abandon_at_post) {
          return;
        }
        self->setSaving(false);
        if (!result) {
          // AttachFile rolls back blob on note-save failure.
          self->setError(errorText(result.error()));
          return;
        }
        self->applyLoadedNote(result.value().note, true);
        const auto& note = result.value().note;
        const QString title = QString::fromStdString(
            note.title.empty() ? note.content.plain_text() : note.title);
        emit self->saved(QString::fromStdString(note.id.value()), title,
                         note.revision, note.pinned);
        emit self->attachmentChanged();
      });
}

void EditorViewModel::removeAttachment(const QString& attachmentId) {
  if (note_id_.isEmpty() || attachmentId.isEmpty()) {
    setError(QStringLiteral("No attachment to remove"));
    return;
  }
  // Flush editor first, then drop AttachmentRefBlock + attempt store remove.
  if (dirty_ || saving_) {
    if (!flushPendingSavesBlocking()) {
      return;
    }
  }
  domain::Note note = noteSnapshot();
  auto blocks = note.content.blocks();
  const std::string id = attachmentId.toStdString();
  const auto before = blocks.size();
  blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                              [&](const domain::ContentBlock& b) {
                                const auto* a =
                                    std::get_if<domain::AttachmentRefBlock>(&b);
                                return a && a->attachment_id.value() == id;
                              }),
               blocks.end());
  if (blocks.size() == before) {
    setError(QStringLiteral("Attachment not in note"));
    return;
  }
  note.content = domain::NoteContent{std::move(blocks)};
  application::SaveNote::Request req;
  req.note = std::move(note);
  req.allow_keep_both = true;
  const int gen = ++save_generation_;
  setSaving(true);
  setError({});
  QPointer<EditorViewModel> self(this);
  const QString expected = note_id_;
  const QString att_id = attachmentId;
  dispatcher_.postResult<application::Result<application::SaveNote::Outcome>>(
      [this, req = std::move(req)]() mutable {
        return save_note_.execute(std::move(req));
      },
      this,
      [self, gen, expected, att_id](
          application::Result<application::SaveNote::Outcome> result) {
        if (!self) return;
        if (gen != self->save_generation_ || self->note_id_ != expected) {
          return;
        }
        self->setSaving(false);
        if (!result) {
          self->setError(errorText(result.error()));
          return;
        }
        self->applyLoadedNote(result.value().saved, true);
        // Best-effort blob cleanup after note save; orphan GC is purge path.
        if (self->attachment_store_) {
          (void)self->attachment_store_->remove(
              domain::AttachmentId{att_id.toStdString()});
        }
        emit self->attachmentChanged();
      });
}

void EditorViewModel::scheduleDebouncedSave() {
  idle_timer_->start();
  if (!max_timer_->isActive()) max_timer_->start();
}

domain::NoteContent EditorViewModel::contentFromEditor() const {
  if (cached_content_valid_) {
    return cached_content_;
  }
  auto parsed = NoteContentDocumentMapper::fromHtml(html_);
  cacheContent(parsed);
  return parsed;
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
  ++structured_op_generation_;
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
        self->cacheContent(note.content);
        self->refreshAttachmentLists(note.content);
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
