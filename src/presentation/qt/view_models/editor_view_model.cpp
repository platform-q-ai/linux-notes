#include "presentation/qt/view_models/editor_view_model.hpp"

#include "application/use_cases/attachments/unref_attachment.hpp"
#include "presentation/qt/view_models/error_text.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextBlock>
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
  if (refuseTrashedMutation("setPinned")) return;
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
  if (refuseTrashedMutation("setPlainText")) return;
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
  // Trashed notes are read-only: keep last loaded HTML, do not dirty/save.
  if (noteTrashed() && !note_id_.isEmpty()) {
    setError(QStringLiteral(
        "Note is in trash. Restore it before editing or saving."));
    // Re-emit current html so QML bindings can snap back if needed.
    emit htmlChanged();
    return;
  }
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
  if (refuseTrashedMutation("toggleInlineStyle")) return;
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

void EditorViewModel::assignTrashMetadata(const domain::Note& note) {
  const bool was_trashed = noteTrashed();
  trashed_at_ms_ = note.trashed_at_ms;
  if (note.trashed_from_folder_id && !note.trashed_from_folder_id->empty()) {
    trashed_from_folder_id_ =
        QString::fromStdString(note.trashed_from_folder_id->value());
  } else {
    trashed_from_folder_id_.clear();
  }
  if (was_trashed != noteTrashed()) {
    emit noteTrashedChanged();
  }
}

bool EditorViewModel::refuseTrashedMutation(const char* action) {
  if (!noteTrashed()) {
    return false;
  }
  Q_UNUSED(action);
  setError(QStringLiteral(
      "Note is in trash. Restore it before editing or saving."));
  return true;
}

void EditorViewModel::applyLoadedNote(const domain::Note& note, bool mark_clean) {
  note_id_ = QString::fromStdString(note.id.value());
  folder_id_ = QString::fromStdString(note.folder_id.value());
  revision_ = note.revision;
  pinned_ = note.pinned;
  created_at_ms_ = note.created_at_ms;
  assignTrashMetadata(note);
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
  if (refuseTrashedMutation("insertChecklist")) return;
  auto content = contentFromEditor();
  auto blocks = content.blocks();
  blocks.emplace_back(domain::ChecklistBlock{
      std::vector<domain::ChecklistItem>{domain::ChecklistItem{false, ""}}});
  content = domain::NoteContent{std::move(blocks)};
  setHtml(NoteContentDocumentMapper::toHtml(content));
}

bool EditorViewModel::toggleChecklistAtPlainOffset(int plainOffset) {
  if (note_id_.isEmpty() || plainOffset < 0) return false;
  if (refuseTrashedMutation("toggleChecklist")) return false;
  // Walk the same serialization as domain::NoteContent::plain_text().
  const auto content = contentFromEditor();
  int pos = 0;
  int block_index = 0;
  bool first_block = true;
  for (const auto& block : content.blocks()) {
    if (!first_block) {
      ++pos;  // plain_text inserts '\n' between top-level blocks
    }
    first_block = false;

    if (const auto* check = std::get_if<domain::ChecklistBlock>(&block)) {
      for (std::size_t i = 0; i < check->items().size(); ++i) {
        if (i > 0) {
          ++pos;  // newline between checklist items
        }
        const QString line =
            (check->items()[i].done ? QStringLiteral("[x] ")
                                    : QStringLiteral("[ ] ")) +
            QString::fromStdString(check->items()[i].text);
        const int line_start = pos;
        const int line_end = pos + line.size();
        if (plainOffset >= line_start && plainOffset <= line_end) {
          toggleChecklistItem(block_index, static_cast<int>(i));
          return true;
        }
        pos = line_end;
      }
      ++block_index;
      continue;
    }
    if (const auto* para = std::get_if<domain::ParagraphBlock>(&block)) {
      std::string t;
      for (const auto& s : para->spans) t += s.text;
      pos += static_cast<int>(t.size());
      ++block_index;
      continue;
    }
    if (const auto* att = std::get_if<domain::AttachmentRefBlock>(&block)) {
      // plain_text emits "[attachment:<display_or_id>]" — not bare label.
      const std::string label =
          att->display_name.empty() ? att->attachment_id.value()
                                    : att->display_name;
      pos += static_cast<int>(std::string("[attachment:]").size() + label.size());
      ++block_index;
      continue;
    }
    ++block_index;
  }
  return false;
}

bool EditorViewModel::toggleChecklistAtDocumentPosition(int documentPosition) {
  if (note_id_.isEmpty() || documentPosition < 0) return false;
  if (refuseTrashedMutation("toggleChecklist")) return false;

  // Rebuild the structured QTextDocument the mapper would show, then map the
  // document character position onto (blockIndex, itemIndex). This matches
  // QML TextArea positionAt() coords, not plain_text offsets.
  const auto content = contentFromEditor();
  QTextDocument doc;
  NoteContentDocumentMapper::applyToDocument(content, doc);
  if (doc.characterCount() <= 0) {
    return false;
  }
  const int max_pos = std::max(0, doc.characterCount() - 1);
  if (documentPosition > max_pos) {
    // Allow clicking at end-of-document; clamp into last character.
    documentPosition = max_pos;
  }
  QTextCursor cursor(&doc);
  cursor.setPosition(documentPosition);
  const QTextBlock block = cursor.block();
  if (!block.isValid()) {
    return false;
  }

  // Prefer exact full-line equality against the visible checklist marker line
  // (stable across HTML round-trips). Suffix/endsWith matching is intentionally
  // rejected: items ["a","ba"] with block "[ ] ba" must not hit endsWith("a").
  // Fall back to QTextBlock ordinal walk when no exact line match exists.
  const QString block_text = block.text();
  int domain_block_index = 0;
  for (const auto& cblock : content.blocks()) {
    if (const auto* check = std::get_if<domain::ChecklistBlock>(&cblock)) {
      const auto& items = check->items();
      for (std::size_t i = 0; i < items.size(); ++i) {
        const QString line =
            (items[i].done ? QStringLiteral("[x] ") : QStringLiteral("[ ] ")) +
            QString::fromStdString(items[i].text);
        if (block_text == line) {
          toggleChecklistItem(domain_block_index, static_cast<int>(i));
          return true;
        }
      }
      ++domain_block_index;
      continue;
    }
    ++domain_block_index;
  }

  // Ordinal fallback: one QTextBlock per paragraph/attachment; one per item.
  int q_block_index = 0;
  const int target_q = block.blockNumber();
  domain_block_index = 0;
  for (const auto& cblock : content.blocks()) {
    if (std::get_if<domain::ParagraphBlock>(&cblock) ||
        std::get_if<domain::AttachmentRefBlock>(&cblock)) {
      if (q_block_index == target_q) {
        return false;
      }
      ++q_block_index;
      ++domain_block_index;
      continue;
    }
    if (const auto* check = std::get_if<domain::ChecklistBlock>(&cblock)) {
      const auto& items = check->items();
      if (items.empty()) {
        if (q_block_index == target_q) {
          toggleChecklistItem(domain_block_index, 0);
          return true;
        }
        ++q_block_index;
      } else {
        for (std::size_t i = 0; i < items.size(); ++i) {
          if (q_block_index == target_q) {
            toggleChecklistItem(domain_block_index, static_cast<int>(i));
            return true;
          }
          ++q_block_index;
        }
      }
      ++domain_block_index;
      continue;
    }
    ++domain_block_index;
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
  if (refuseTrashedMutation("toggleChecklistItem")) return;
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
  if (refuseTrashedMutation("attachLocalFile")) return;
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
  if (refuseTrashedMutation("removeAttachment")) return;
  // Untrusted UI/document ids must be opaque-safe before any store remove.
  if (!domain::AttachmentId::is_opaque_safe(attachmentId.toStdString())) {
    setError(QStringLiteral("Invalid attachment id"));
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
        // Unref-only blob cleanup: other notes may still share this opaque id
        // (paste / degraded keep-both). Never delete while any note references it.
        if (self->attachment_store_) {
          application::UnrefAttachment unref{self->load_note_.reader(),
                                             *self->attachment_store_};
          (void)unref.execute(domain::AttachmentId{att_id.toStdString()},
                              domain::NoteId{expected.toStdString()});
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
  // Preserve soft-delete metadata so ordinary saves cannot resurrect trash.
  note.trashed_at_ms = trashed_at_ms_;
  if (!trashed_from_folder_id_.isEmpty()) {
    note.trashed_from_folder_id =
        domain::FolderId{trashed_from_folder_id_.toStdString()};
  }
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
  const bool was_trashed = noteTrashed();
  note_id_.clear();
  folder_id_.clear();
  html_.clear();
  plain_.clear();
  last_saved_html_.clear();
  revision_ = 0;
  created_at_ms_ = 0;
  trashed_at_ms_ = 0;
  trashed_from_folder_id_.clear();
  pinned_ = false;
  setDirty(false);
  setError({});
  emit noteIdChanged();
  emit htmlChanged();
  emit plainTextChanged();
  emit revisionChanged();
  emit pinnedChanged();
  if (was_trashed) {
    emit noteTrashedChanged();
  }
  emitSaveState();
}

void EditorViewModel::applySaveSuccess(const application::SaveNote::Outcome& out,
                                       const QString& html_snapshot) {
  note_id_ = QString::fromStdString(out.saved.id.value());
  folder_id_ = QString::fromStdString(out.saved.folder_id.value());
  revision_ = out.saved.revision;
  pinned_ = out.saved.pinned;
  created_at_ms_ = out.saved.created_at_ms;
  assignTrashMetadata(out.saved);
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
  if (flushing_) {
    setError(QStringLiteral("Save in progress; try again shortly."));
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
        // Route through applyLoadedNote so trash metadata is always applied.
        self->applyLoadedNote(note, true);
      });
}

void EditorViewModel::closeNote() {
  if (flushing_) {
    setError(QStringLiteral("Save in progress; try again shortly."));
    return;
  }
  idle_timer_->stop();
  max_timer_->stop();
  if (!note_id_.isEmpty() && (dirty_ || saving_)) {
    // Never body-save a trashed note (would fight soft-delete policy).
    if (noteTrashed()) {
      setDirty(false);
      queued_resave_ = false;
    } else if (!flushPendingSavesBlocking()) {
      return;
    }
  }
  ++load_generation_;
  abandonInFlightUi();
  clearEditorState();
}

void EditorViewModel::discardEditorWithoutFlush() {
  idle_timer_->stop();
  max_timer_->stop();
  ++load_generation_;
  abandonInFlightUi();
  setDirty(false);
  queued_resave_ = false;
  clearEditorState();
}

void EditorViewModel::saveNow() {
  if (refuseTrashedMutation("saveNow")) return;
  idle_timer_->stop();
  max_timer_->stop();
  performSave(true);
}

void EditorViewModel::performSave(bool /*from_max_timer*/) {
  if (note_id_.isEmpty() || !dirty_) return;
  if (noteTrashed()) {
    setDirty(false);
    queued_resave_ = false;
    (void)refuseTrashedMutation("performSave");
    return;
  }
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
  if (note_id_.isEmpty() || noteTrashed() || (!dirty_ && !saving_)) {
    return std::nullopt;
  }
  application::SaveNote::Request req;
  req.note = noteSnapshot();
  req.allow_keep_both = true;
  return req;
}

void EditorViewModel::flushDirtySyncRequest(std::function<void()> done) {
  idle_timer_->stop();
  max_timer_->stop();
  if (note_id_.isEmpty() || !dirty_ || noteTrashed()) {
    if (noteTrashed()) {
      setDirty(false);
      queued_resave_ = false;
    }
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
  // Soft-deleted notes must not body-save (would clear or fight trash metadata).
  if (noteTrashed()) {
    setDirty(false);
    queued_resave_ = false;
    setSaving(false);
    return true;
  }
  // Nested flush is unsafe: prior AllEvents pump allowed re-entrant close/open.
  if (flushing_) {
    return false;
  }
  flushing_ = true;
  struct FlushGuard {
    bool& flag;
    ~FlushGuard() { flag = false; }
  } guard{flushing_};

  // Capture whether an async save was in flight before the IO-only drain.
  // runBlocking waits for the worker to finish SaveNote but does NOT deliver
  // the GUI QueuedConnection completion — revision_/dirty_ stay stale unless
  // we reconcile here (without processEvents(AllEvents)).
  const bool was_saving = saving_;
  if (saving_) {
    // Drain the IO strand only. Do NOT process user-input / QML events here —
    // AllEvents re-entrancy caused nested close/switch mid-flush.
    dispatcher_.runBlocking([] {});
  }

  if (was_saving && !note_id_.isEmpty() && !noteTrashed()) {
    application::Result<domain::Note> loaded =
        application::Result<domain::Note>::fail(
            {application::ErrorKind::StorageFailure, "flush reload skipped"});
    const domain::NoteId id{note_id_.toStdString()};
    dispatcher_.runBlocking([this, &loaded, id]() {
      loaded = load_note_.execute(id);
    });
    if (loaded) {
      const domain::Note& note = loaded.value();
      // Always refresh CAS base so any follow-up write uses the store head.
      if (revision_ != note.revision) {
        revision_ = note.revision;
        emit revisionChanged();
      }
      pinned_ = note.pinned;
      created_at_ms_ = note.created_at_ms;
      assignTrashMetadata(note);
      // If editor content already matches the persisted note, the in-flight
      // save committed our snapshot — clear dirty and skip a second SaveNote
      // that would otherwise RevisionConflict → spurious keep-both.
      const domain::NoteContent editor_content = contentFromEditor();
      if (editor_content.plain_text() == note.content.plain_text()) {
        last_saved_html_ = html_;
        setDirty(false);
        queued_resave_ = false;
      }
    }
    setSaving(false);
  }

  if (!dirty_) {
    queued_resave_ = false;
    setSaving(false);
    return error_.isEmpty();
  }

  // Dirty remains only when the editor diverged after the in-flight snapshot
  // (or there was no in-flight save). revision_ is store-fresh when was_saving.
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
