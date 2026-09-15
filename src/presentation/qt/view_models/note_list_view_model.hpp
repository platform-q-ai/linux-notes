#pragma once

#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/delete_note.hpp"
#include "application/use_cases/notes/list_notes.hpp"
#include "application/use_cases/notes/list_trashed_notes.hpp"
#include "application/use_cases/notes/move_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/restore_note.hpp"
#include "application/use_cases/notes/search_notes.hpp"
#include "presentation/qt/execution/use_case_dispatcher.hpp"
#include "presentation/qt/models/note_list_model.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

namespace notes::presentation {

class NoteListViewModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(NoteListModel* model READ model CONSTANT)
  Q_PROPERTY(QString folderId READ folderId WRITE setFolderId NOTIFY
                 folderIdChanged)
  Q_PROPERTY(QString selectedNoteId READ selectedNoteId WRITE setSelectedNoteId
                 NOTIFY selectedNoteIdChanged)
  Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY
                 searchQueryChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
  Q_PROPERTY(bool showingTrash READ showingTrash NOTIFY showingTrashChanged)

public:
  // Order matches composition_root.hpp (extended for trash/move).
  NoteListViewModel(application::ListNotes& list_notes,
                    application::CreateNote& create_note,
                    application::DeleteNote& delete_note,
                    application::SearchNotes& search_notes,
                    application::ListTrashedNotes& list_trashed,
                    application::RestoreNote& restore_note,
                    application::PurgeNote& purge_note,
                    application::MoveNote& move_note,
                    UseCaseDispatcher& dispatcher, QObject* parent = nullptr);

  [[nodiscard]] NoteListModel* model() const { return model_; }
  [[nodiscard]] QString folderId() const { return folder_id_; }
  void setFolderId(const QString& id);
  [[nodiscard]] QString selectedNoteId() const { return selected_note_id_; }
  void setSelectedNoteId(const QString& id);
  [[nodiscard]] QString searchQuery() const { return search_query_; }
  void setSearchQuery(const QString& q);
  [[nodiscard]] bool busy() const { return busy_; }
  [[nodiscard]] QString errorString() const { return error_; }
  [[nodiscard]] bool searching() const { return searching_; }
  [[nodiscard]] bool showingTrash() const { return showing_trash_; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void createNote();
  // Soft-delete (trash). Default delete path.
  Q_INVOKABLE void deleteNote(const QString& noteId);
  Q_INVOKABLE void trashNote(const QString& noteId);
  Q_INVOKABLE void restoreNote(const QString& noteId);
  // Permanent delete — call only after UI confirmation.
  Q_INVOKABLE void purgeNote(const QString& noteId);
  Q_INVOKABLE void moveNote(const QString& noteId, const QString& targetFolderId);
  Q_INVOKABLE void showTrash();
  Q_INVOKABLE void hideTrash();
  Q_INVOKABLE void selectNoteAt(int row);
  Q_INVOKABLE void applySummaryTitle(const QString& noteId, const QString& title,
                                     qint64 revision, bool pinned);

signals:
  void folderIdChanged();
  void selectedNoteIdChanged();
  void searchQueryChanged();
  void busyChanged();
  void errorStringChanged();
  void searchingChanged();
  void showingTrashChanged();
  void noteCreated(const QString& noteId);
  void openNoteRequested(const QString& noteId);
  void noteDeleted(const QString& noteId);
  void notePurged(const QString& noteId);
  void noteRestored(const QString& noteId);
  void noteMoved(const QString& noteId, const QString& folderId);

private:
  void setBusy(bool v);
  void setError(QString e);
  void setSearching(bool v);
  void setShowingTrash(bool v);
  void runSearch();
  void runListTrashed();
  // Drop selection when the selected id is not in the current model rows
  // (after trash/move/search/folder switch) so editor/list stay coherent.
  void pruneSelectionToModel();
  void clearSelectionQuiet();

  application::ListNotes& list_notes_;
  application::CreateNote& create_note_;
  application::DeleteNote& delete_note_;
  application::SearchNotes& search_notes_;
  application::ListTrashedNotes& list_trashed_;
  application::RestoreNote& restore_note_;
  application::PurgeNote& purge_note_;
  application::MoveNote& move_note_;
  UseCaseDispatcher& dispatcher_;
  NoteListModel* model_{nullptr};
  QString folder_id_;
  QString selected_note_id_;
  QString search_query_;
  bool busy_{false};
  bool searching_{false};
  bool showing_trash_{false};
  QString error_;
  int search_generation_{0};
};

}  // namespace notes::presentation
