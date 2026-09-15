#pragma once

#include "application/ports/attachments/attachment_store.hpp"
#include "application/use_cases/attachments/attach_file.hpp"
#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/use_cases/notes/toggle_checklist_item.hpp"
#include "domain/notes/note.hpp"
#include "presentation/qt/execution/use_case_dispatcher.hpp"
#include "presentation/qt/mapping/note_content_document_mapper.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <optional>
#include <string>

namespace notes::presentation {

class EditorViewModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString noteId READ noteId NOTIFY noteIdChanged)
  Q_PROPERTY(QString html READ html WRITE setHtml NOTIFY htmlChanged)
  Q_PROPERTY(QString plainText READ plainText NOTIFY plainTextChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
  Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  Q_PROPERTY(QString saveState READ saveState NOTIFY saveStateChanged)
  Q_PROPERTY(qint64 revision READ revision NOTIFY revisionChanged)
  Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
  Q_PROPERTY(bool structuredOpsAvailable READ structuredOpsAvailable
                 NOTIFY structuredOpsChanged)

public:
  // toggle/attach/store may be null (tests / reduced shells). Production root
  // wires ToggleChecklistItem + AttachFile + AttachmentStore.
  EditorViewModel(application::LoadNote& load_note,
                  application::SaveNote& save_note,
                  UseCaseDispatcher& dispatcher,
                  application::ToggleChecklistItem* toggle_checklist = nullptr,
                  application::AttachFile* attach_file = nullptr,
                  application::AttachmentStore* attachment_store = nullptr,
                  QObject* parent = nullptr);

  [[nodiscard]] QString noteId() const { return note_id_; }
  [[nodiscard]] QString html() const { return html_; }
  void setHtml(const QString& html);
  [[nodiscard]] QString plainText() const { return plain_; }
  [[nodiscard]] bool dirty() const { return dirty_; }
  [[nodiscard]] bool saving() const { return saving_; }
  [[nodiscard]] bool loading() const { return loading_; }
  [[nodiscard]] QString errorString() const { return error_; }
  [[nodiscard]] QString saveState() const;
  [[nodiscard]] qint64 revision() const { return revision_; }
  [[nodiscard]] bool pinned() const { return pinned_; }
  void setPinned(bool pinned);
  [[nodiscard]] bool canUndo() const { return can_undo_; }
  [[nodiscard]] bool canRedo() const { return can_redo_; }
  [[nodiscard]] bool structuredOpsAvailable() const {
    return toggle_checklist_ != nullptr || attach_file_ != nullptr;
  }

  Q_INVOKABLE void openNote(const QString& noteId);
  Q_INVOKABLE void loadNote(const QString& noteId) { openNote(noteId); }
  Q_INVOKABLE void closeNote();
  Q_INVOKABLE void saveNow();
  Q_INVOKABLE void markUndoRedo(bool canUndo, bool canRedo);
  Q_INVOKABLE void setPlainText(const QString& plain);
  Q_INVOKABLE void toggleInlineStyle(int selectionStart, int selectionEnd,
                                     const QString& style);
  Q_INVOKABLE bool flushPendingSavesBlocking();

  // Structured content ops (ports/use-cases). Safe-path attach only.
  Q_INVOKABLE void insertChecklist();
  Q_INVOKABLE void toggleChecklistItem(int blockIndex, int itemIndex);
  // Toggle the checklist item whose marker contains plain-text offset.
  // Returns true if a checklist marker was found and toggled.
  Q_INVOKABLE bool toggleChecklistAtPlainOffset(int plainOffset);
  Q_INVOKABLE void attachLocalFile(const QString& localPath);
  Q_INVOKABLE void removeAttachment(const QString& attachmentId);
  // Attachment ids currently in editor content (for UI list / delete).
  Q_INVOKABLE QStringList attachmentIds() const;
  Q_INVOKABLE QStringList attachmentNames() const;

  bool flushSync();
  [[nodiscard]] std::optional<application::SaveNote::Request>
  pendingSaveRequest() const;
  void flushDirtySyncRequest(std::function<void()> done);

signals:
  void noteIdChanged();
  void htmlChanged();
  void plainTextChanged();
  void dirtyChanged();
  void savingChanged();
  void loadingChanged();
  void errorStringChanged();
  void saveStateChanged();
  void revisionChanged();
  void pinnedChanged();
  void undoStateChanged();
  void structuredOpsChanged();
  void saved(const QString& noteId, const QString& title, qint64 revision,
             bool pinned);
  void keepBothNotice(const QString& message);
  void attachmentChanged();

private:
  void setDirty(bool v);
  void setSaving(bool v);
  void setLoading(bool v);
  void setError(QString e);
  void emitSaveState();
  void scheduleDebouncedSave();
  void performSave(bool from_max_timer);
  void clearEditorState();
  void abandonInFlightUi();
  void applySaveSuccess(const application::SaveNote::Outcome& out,
                        const QString& html_snapshot);
  void applyLoadedNote(const domain::Note& note, bool mark_clean);
  [[nodiscard]] domain::NoteContent contentFromEditor() const;
  [[nodiscard]] domain::Note noteSnapshot() const;
  static std::string title_from_plain(const QString& plain);
  [[nodiscard]] static bool is_safe_local_file_path(const QString& path,
                                                    QString* reason);

  application::LoadNote& load_note_;
  application::SaveNote& save_note_;
  UseCaseDispatcher& dispatcher_;
  application::ToggleChecklistItem* toggle_checklist_{nullptr};
  application::AttachFile* attach_file_{nullptr};
  application::AttachmentStore* attachment_store_{nullptr};

  QString note_id_;
  QString folder_id_;
  QString html_;
  QString plain_;
  qint64 revision_{0};
  qint64 created_at_ms_{0};
  bool pinned_{false};
  bool dirty_{false};
  bool saving_{false};
  bool loading_{false};
  bool can_undo_{false};
  bool can_redo_{false};
  bool applying_load_{false};
  bool queued_resave_{false};
  QString error_;

  QTimer* idle_timer_{nullptr};
  QTimer* max_timer_{nullptr};
  int load_generation_{0};
  int save_generation_{0};
  // Separate from load_generation_ so openNote generation bumps do not
  // spuriously drop in-flight toggle/attach completions on the same note.
  int structured_op_generation_{0};
  QString last_saved_html_;
  // Last known domain content (avoids HTML reparse edge cases for attachment UI).
  mutable domain::NoteContent cached_content_{};
  mutable bool cached_content_valid_{false};
  QStringList attachment_ids_;
  QStringList attachment_names_;
  void invalidateContentCache() const { cached_content_valid_ = false; }
  void cacheContent(domain::NoteContent c) const {
    cached_content_ = std::move(c);
    cached_content_valid_ = true;
  }
  void refreshAttachmentLists(const domain::NoteContent& content);
};

}  // namespace notes::presentation
