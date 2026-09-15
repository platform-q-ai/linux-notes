#pragma once

#include "domain/notes/note.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

namespace notes::presentation {

class NoteListModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    FolderIdRole,
    TitleRole,
    ModifiedAtRole,
    RevisionRole,
    PinnedRole
  };

  explicit NoteListModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setNotes(std::vector<domain::NoteSummary> notes);
  void upsert(const domain::NoteSummary& summary);
  void removeById(const domain::NoteId& id);
  [[nodiscard]] domain::NoteId noteIdAt(int row) const;
  [[nodiscard]] int rowForNoteId(const domain::NoteId& id) const;

private:
  void sortInPlace();

  QVector<domain::NoteSummary> notes_;
};

}  // namespace notes::presentation
