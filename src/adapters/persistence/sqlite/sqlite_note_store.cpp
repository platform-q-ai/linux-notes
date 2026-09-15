#include "adapters/persistence/sqlite/sqlite_note_store.hpp"

#include "adapters/persistence/sqlite/content_codec.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>

namespace notes::adapters::persistence {
namespace {

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

domain::Note row_to_note(Stmt& st) {
  domain::Note n;
  n.id = domain::NoteId{st.column_text(0)};
  n.folder_id = domain::FolderId{st.column_text(1)};
  n.title = st.column_text(2);
  n.content = decode_content(st.column_text(3));
  n.created_at_ms = st.column_int64(4);
  n.modified_at_ms = st.column_int64(5);
  n.revision = st.column_int64(6);
  n.pinned = st.column_int64(7) != 0;
  n.trashed_at_ms = st.column_int64(8);
  if (!st.column_is_null(9) && !st.column_text(9).empty()) {
    n.trashed_from_folder_id = domain::FolderId{st.column_text(9)};
  }
  return n;
}

domain::NoteSummary row_to_summary(Stmt& st) {
  domain::NoteSummary s;
  s.id = domain::NoteId{st.column_text(0)};
  s.folder_id = domain::FolderId{st.column_text(1)};
  s.title = st.column_text(2);
  s.modified_at_ms = st.column_int64(3);
  s.revision = st.column_int64(4);
  s.pinned = st.column_int64(5) != 0;
  s.trashed_at_ms = st.column_int64(6);
  return s;
}

application::Result<void> fail_storage(const SqliteDb& db) {
  return application::Result<void>::fail(
      {application::ErrorKind::StorageFailure, db.last_error()});
}

constexpr const char* kNoteSelect =
    "SELECT id,folder_id,title,body,created_at,modified_at,revision,"
    "pinned,trashed_at,trashed_from_folder_id FROM notes WHERE id=?";

}  // namespace

SqliteNoteStore::SqliteNoteStore(std::shared_ptr<SqliteDb> db)
    : db_(std::move(db)) {}

application::Result<domain::Note> SqliteNoteStore::load(
    const domain::NoteId& id) const {
  Stmt st(db_->handle(), kNoteSelect);
  if (!st.valid()) {
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  st.bind_text(1, id.value());
  const int rc = st.step();
  if (rc == SQLITE_DONE) {
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::NotFound, "note not found"});
  }
  if (rc != SQLITE_ROW) {
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  return application::Result<domain::Note>::ok(row_to_note(st));
}

application::Result<std::vector<domain::NoteSummary>> SqliteNoteStore::list(
    const domain::FolderId& folder_id) const {
  Stmt st(db_->handle(),
          "SELECT id,folder_id,title,modified_at,revision,pinned,trashed_at "
          "FROM notes WHERE folder_id=? AND trashed_at=0 "
          "ORDER BY pinned DESC, modified_at DESC");
  if (!st.valid()) {
    return application::Result<std::vector<domain::NoteSummary>>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  st.bind_text(1, folder_id.value());
  std::vector<domain::NoteSummary> out;
  while (true) {
    const int rc = st.step();
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) {
      return application::Result<std::vector<domain::NoteSummary>>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
    out.push_back(row_to_summary(st));
  }
  return application::Result<std::vector<domain::NoteSummary>>::ok(
      std::move(out));
}

application::Result<std::vector<domain::NoteSummary>>
SqliteNoteStore::list_trashed() const {
  Stmt st(db_->handle(),
          "SELECT id,folder_id,title,modified_at,revision,pinned,trashed_at "
          "FROM notes WHERE trashed_at>0 ORDER BY trashed_at DESC");
  if (!st.valid()) {
    return application::Result<std::vector<domain::NoteSummary>>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  std::vector<domain::NoteSummary> out;
  while (true) {
    const int rc = st.step();
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) {
      return application::Result<std::vector<domain::NoteSummary>>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
    out.push_back(row_to_summary(st));
  }
  return application::Result<std::vector<domain::NoteSummary>>::ok(
      std::move(out));
}

application::Result<std::vector<domain::NoteId>> SqliteNoteStore::all_note_ids()
    const {
  Stmt st(db_->handle(), "SELECT id FROM notes");
  if (!st.valid()) {
    return application::Result<std::vector<domain::NoteId>>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  std::vector<domain::NoteId> out;
  while (true) {
    const int rc = st.step();
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) {
      return application::Result<std::vector<domain::NoteId>>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
    out.emplace_back(st.column_text(0));
  }
  return application::Result<std::vector<domain::NoteId>>::ok(std::move(out));
}

application::Result<domain::Note> SqliteNoteStore::save(
    const domain::Note& note) {
  auto begin = db_->begin_immediate();
  if (!begin) {
    return application::Result<domain::Note>::fail(begin.error());
  }

  Stmt sel(db_->handle(),
           "SELECT revision, trashed_at, trashed_from_folder_id, folder_id "
           "FROM notes WHERE id=?");
  if (!sel.valid()) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  sel.bind_text(1, note.id.value());
  const int src = sel.step();
  domain::Note stored = note;

  if (src == SQLITE_ROW) {
    const auto current_rev = sel.column_int64(0);
    const auto current_trashed_at = sel.column_int64(1);
    if (current_rev != note.revision) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::RevisionConflict, "CAS failed"});
    }
    // Trash is durable: only restore() may clear soft-delete columns. A stale
    // editor body-save with trashed_at_ms=0 must not resurrect the note.
    if (current_trashed_at > 0 && note.trashed_at_ms <= 0) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::ValidationFailed,
           "cannot clear trash via save; use restore"});
    }
    // When the row is trashed, pin trash metadata from storage so callers
    // cannot partially corrupt park state even with matching revision.
    if (current_trashed_at > 0) {
      stored.trashed_at_ms = current_trashed_at;
      if (!sel.column_is_null(2) && !sel.column_text(2).empty()) {
        stored.trashed_from_folder_id = domain::FolderId{sel.column_text(2)};
      } else {
        stored.trashed_from_folder_id = std::nullopt;
      }
      stored.folder_id = domain::FolderId{sel.column_text(3)};
    }
    stored.revision = note.revision + 1;
    Stmt upd(db_->handle(),
             "UPDATE notes SET folder_id=?, title=?, body=?, modified_at=?, "
             "revision=?, pinned=?, trashed_at=?, trashed_from_folder_id=? "
             "WHERE id=? AND revision=?");
    if (!upd.valid()) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
    const auto body = encode_content(note.content);
    upd.bind_text(1, stored.folder_id.value());
    upd.bind_text(2, note.title);
    upd.bind_blob(3, body);
    upd.bind_int64(4, note.modified_at_ms);
    upd.bind_int64(5, stored.revision);
    upd.bind_int64(6, note.pinned ? 1 : 0);
    upd.bind_int64(7, stored.trashed_at_ms);
    if (stored.trashed_from_folder_id &&
        !stored.trashed_from_folder_id->empty()) {
      upd.bind_text(8, stored.trashed_from_folder_id->value());
    } else {
      upd.bind_null(8);
    }
    upd.bind_text(9, note.id.value());
    upd.bind_int64(10, current_rev);
    if (upd.step() != SQLITE_DONE || sqlite3_changes(db_->handle()) != 1) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::RevisionConflict, "CAS update missed"});
    }
  } else if (src == SQLITE_DONE) {
    if (note.revision != 0) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::RevisionConflict, "missing base"});
    }
    stored.revision = 1;
    Stmt ins(db_->handle(),
             "INSERT INTO notes(id,folder_id,title,body,created_at,modified_at,"
             "revision,pinned,trashed_at,trashed_from_folder_id) "
             "VALUES(?,?,?,?,?,?,?,?,?,?)");
    if (!ins.valid()) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
    const auto body = encode_content(note.content);
    ins.bind_text(1, note.id.value());
    ins.bind_text(2, note.folder_id.value());
    ins.bind_text(3, note.title);
    ins.bind_blob(4, body);
    ins.bind_int64(5, note.created_at_ms);
    ins.bind_int64(6, note.modified_at_ms);
    ins.bind_int64(7, stored.revision);
    ins.bind_int64(8, note.pinned ? 1 : 0);
    ins.bind_int64(9, note.trashed_at_ms);
    if (note.trashed_from_folder_id && !note.trashed_from_folder_id->empty()) {
      ins.bind_text(10, note.trashed_from_folder_id->value());
    } else {
      ins.bind_null(10);
    }
    if (ins.step() != SQLITE_DONE) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
  } else {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }

  // Search index is part of the same atomic save — fail closed on any error.
  // Trashed notes keep index rows for restore; search() filters trashed_at.
  Stmt del_s(db_->handle(), "DELETE FROM notes_search WHERE note_id=?");
  if (!del_s.valid()) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  del_s.bind_text(1, note.id.value());
  if (del_s.step() != SQLITE_DONE) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  Stmt ins_s(db_->handle(),
             "INSERT INTO notes_search(note_id, search_text) VALUES(?,?)");
  if (!ins_s.valid()) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  const auto hay = to_lower(note.title + " " + note.content.plain_text());
  ins_s.bind_text(1, note.id.value());
  ins_s.bind_text(2, hay);
  if (ins_s.step() != SQLITE_DONE) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }

  auto c = db_->commit();
  if (!c) {
    return application::Result<domain::Note>::fail(c.error());
  }
  return application::Result<domain::Note>::ok(std::move(stored));
}

application::Result<void> SqliteNoteStore::trash(const domain::NoteId& id,
                                                 std::int64_t trashed_at_ms) {
  if (trashed_at_ms <= 0) {
    return application::Result<void>::fail(
        {application::ErrorKind::ValidationFailed,
         "trashed_at_ms must be positive"});
  }
  auto begin = db_->begin_immediate();
  if (!begin) return begin;

  Stmt sel(db_->handle(),
           "SELECT folder_id, trashed_at FROM notes WHERE id=?");
  if (!sel.valid()) {
    (void)db_->rollback();
    return fail_storage(*db_);
  }
  sel.bind_text(1, id.value());
  const int rc = sel.step();
  if (rc == SQLITE_DONE) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::NotFound, "note not found"});
  }
  if (rc != SQLITE_ROW) {
    (void)db_->rollback();
    return fail_storage(*db_);
  }
  const auto folder = sel.column_text(0);
  const auto already = sel.column_int64(1);
  if (already > 0) {
    // Idempotent: already trashed.
    (void)db_->rollback();
    return application::Result<void>::ok();
  }

  // Park under root so the prior folder can still be deleted without orphans;
  // list/search already exclude trashed_at>0. Bump revision so in-flight body
  // saves holding the pre-trash revision fail CAS instead of resurrecting.
  Stmt upd(db_->handle(),
           "UPDATE notes SET trashed_at=?, trashed_from_folder_id=?, "
           "folder_id='root', modified_at=?, revision=revision+1 "
           "WHERE id=? AND trashed_at=0");
  if (!upd.valid()) {
    (void)db_->rollback();
    return fail_storage(*db_);
  }
  upd.bind_int64(1, trashed_at_ms);
  upd.bind_text(2, folder);
  upd.bind_int64(3, trashed_at_ms);
  upd.bind_text(4, id.value());
  if (upd.step() != SQLITE_DONE || sqlite3_changes(db_->handle()) != 1) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::StorageFailure, "trash update missed"});
  }
  return db_->commit();
}

application::Result<domain::Note> SqliteNoteStore::restore(
    const domain::NoteId& id, const domain::FolderId& restore_folder_id) {
  auto begin = db_->begin_immediate();
  if (!begin) {
    return application::Result<domain::Note>::fail(begin.error());
  }

  Stmt sel(db_->handle(),
           "SELECT trashed_at FROM notes WHERE id=?");
  if (!sel.valid()) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  sel.bind_text(1, id.value());
  const int rc = sel.step();
  if (rc == SQLITE_DONE) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::NotFound, "note not found"});
  }
  if (rc != SQLITE_ROW) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  if (sel.column_int64(0) <= 0) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::ValidationFailed, "note is not in trash"});
  }

  // Verify target folder exists before rewrite.
  Stmt fchk(db_->handle(), "SELECT 1 FROM folders WHERE id=?");
  if (!fchk.valid()) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  fchk.bind_text(1, restore_folder_id.value());
  if (fchk.step() != SQLITE_ROW) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::NotFound, "restore folder not found"});
  }

  // Explicit restore path: clear trash columns and bump revision so any stale
  // pre-restore snapshot cannot race the restored row via CAS.
  Stmt upd(db_->handle(),
           "UPDATE notes SET folder_id=?, trashed_at=0, "
           "trashed_from_folder_id=NULL, "
           "modified_at=CAST(strftime('%s','now') AS INTEGER)*1000, "
           "revision=revision+1 "
           "WHERE id=? AND trashed_at>0");
  if (!upd.valid()) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  upd.bind_text(1, restore_folder_id.value());
  upd.bind_text(2, id.value());
  if (upd.step() != SQLITE_DONE || sqlite3_changes(db_->handle()) != 1) {
    (void)db_->rollback();
    return application::Result<domain::Note>::fail(
        {application::ErrorKind::StorageFailure, "restore update missed"});
  }
  auto c = db_->commit();
  if (!c) {
    return application::Result<domain::Note>::fail(c.error());
  }
  return load(id);
}

application::Result<void> SqliteNoteStore::remove(const domain::NoteId& id) {
  auto begin = db_->begin_immediate();
  if (!begin) return begin;
  Stmt st(db_->handle(), "DELETE FROM notes WHERE id=?");
  if (!st.valid()) {
    (void)db_->rollback();
    return fail_storage(*db_);
  }
  st.bind_text(1, id.value());
  if (st.step() != SQLITE_DONE) {
    (void)db_->rollback();
    return fail_storage(*db_);
  }
  if (sqlite3_changes(db_->handle()) == 0) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::NotFound, "note not found"});
  }
  Stmt ds(db_->handle(), "DELETE FROM notes_search WHERE note_id=?");
  if (!ds.valid()) {
    (void)db_->rollback();
    return fail_storage(*db_);
  }
  ds.bind_text(1, id.value());
  if (ds.step() != SQLITE_DONE) {
    (void)db_->rollback();
    return fail_storage(*db_);
  }
  return db_->commit();
}

application::Result<std::vector<domain::NoteSummary>>
SqliteNoteStore::search(const std::string& query) const {
  Stmt st(db_->handle(),
          "SELECT n.id,n.folder_id,n.title,n.modified_at,n.revision,n.pinned,"
          "n.trashed_at "
          "FROM notes n JOIN notes_search s ON s.note_id=n.id "
          "WHERE n.trashed_at=0 AND s.search_text LIKE ? "
          "ORDER BY n.pinned DESC, n.modified_at DESC LIMIT 100");
  if (!st.valid()) {
    return application::Result<std::vector<domain::NoteSummary>>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  st.bind_text(1, "%" + to_lower(query) + "%");
  std::vector<domain::NoteSummary> out;
  while (true) {
    const int rc = st.step();
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) {
      return application::Result<std::vector<domain::NoteSummary>>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
    out.push_back(row_to_summary(st));
  }
  return application::Result<std::vector<domain::NoteSummary>>::ok(
      std::move(out));
}

}  // namespace notes::adapters::persistence
