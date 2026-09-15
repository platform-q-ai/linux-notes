#include "presentation/qt/models/note_list_model.hpp"

#include <QDateTime>

#include <algorithm>

namespace notes::presentation {

NoteListModel::NoteListModel(QObject* parent) : QAbstractListModel(parent) {}

void NoteListModel::setNotes(std::vector<domain::NoteSummary> notes) {
  beginResetModel();
  notes_.clear();
  notes_.reserve(static_cast<int>(notes.size()));
  for (auto& n : notes) {
    notes_.push_back(std::move(n));
  }
  sortInPlace();
  endResetModel();
}

void NoteListModel::sortInPlace() {
  std::sort(notes_.begin(), notes_.end(),
            [](const domain::NoteSummary& a, const domain::NoteSummary& b) {
              if (a.pinned != b.pinned) {
                return a.pinned && !b.pinned;
              }
              return a.modified_at_ms > b.modified_at_ms;
            });
}

void NoteListModel::upsert(const domain::NoteSummary& summary) {
  beginResetModel();
  const int existing = rowForNoteId(summary.id);
  if (existing >= 0) {
    notes_[existing] = summary;
  } else {
    notes_.push_back(summary);
  }
  sortInPlace();
  endResetModel();
}

void NoteListModel::updateIfPresent(const domain::NoteSummary& summary) {
  const int existing = rowForNoteId(summary.id);
  if (existing < 0) {
    return;
  }
  beginResetModel();
  notes_[existing] = summary;
  sortInPlace();
  endResetModel();
}

void NoteListModel::removeById(const domain::NoteId& id) {
  const int row = rowForNoteId(id);
  if (row < 0) {
    return;
  }
  beginRemoveRows({}, row, row);
  notes_.removeAt(row);
  endRemoveRows();
}

domain::NoteId NoteListModel::noteIdAt(int row) const {
  if (row < 0 || row >= notes_.size()) {
    return {};
  }
  return notes_[row].id;
}

int NoteListModel::rowForNoteId(const domain::NoteId& id) const {
  for (int i = 0; i < notes_.size(); ++i) {
    if (notes_[i].id == id) {
      return i;
    }
  }
  return -1;
}

bool NoteListModel::contains(const domain::NoteId& id) const {
  return rowForNoteId(id) >= 0;
}

int NoteListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return notes_.size();
}

QVariant NoteListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= notes_.size()) {
    return {};
  }
  const auto& n = notes_[index.row()];
  switch (role) {
    case IdRole:
      return QString::fromStdString(n.id.value());
    case FolderIdRole:
      return QString::fromStdString(n.folder_id.value());
    case Qt::DisplayRole:
    case TitleRole:
      return QString::fromStdString(n.title.empty() ? n.preview : n.title);
    case ModifiedAtRole:
      return QDateTime::fromMSecsSinceEpoch(n.modified_at_ms);
    case RevisionRole:
      return static_cast<qint64>(n.revision);
    case PinnedRole:
      return n.pinned;
    default:
      return {};
  }
}

QHash<int, QByteArray> NoteListModel::roleNames() const {
  return {
      {IdRole, "noteId"},
      {FolderIdRole, "folderId"},
      {TitleRole, "title"},
      {ModifiedAtRole, "modifiedAt"},
      {RevisionRole, "revision"},
      {PinnedRole, "pinned"},
      {Qt::DisplayRole, "display"},
  };
}

}  // namespace notes::presentation
