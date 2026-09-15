#pragma once

#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/delete_note.hpp"
#include "application/use_cases/notes/list_notes.hpp"
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

public:
  // Order matches composition_root.hpp
  NoteListViewModel(application::ListNotes& list_notes,
                    application::CreateNote& create_note,
                    application::DeleteNote& delete_note,
                    application::SearchNotes& search_notes,
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

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void createNote();
  Q_INVOKABLE void deleteNote(const QString& noteId);
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
  void noteCreated(const QString& noteId);
  void openNoteRequested(const QString& noteId);
  void noteDeleted(const QString& noteId);

private:
  void setBusy(bool v);
  void setError(QString e);
  void setSearching(bool v);
  void runSearch();

  application::ListNotes& list_notes_;
  application::CreateNote& create_note_;
  application::DeleteNote& delete_note_;
  application::SearchNotes& search_notes_;
  UseCaseDispatcher& dispatcher_;
  NoteListModel* model_{nullptr};
  QString folder_id_;
  QString selected_note_id_;
  QString search_query_;
  bool busy_{false};
  bool searching_{false};
  QString error_;
  int search_generation_{0};
};

}  // namespace notes::presentation
