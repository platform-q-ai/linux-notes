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
  return s;
}

application::Result<void> fail_storage(const SqliteDb& db) {
  return application::Result<void>::fail(
      {application::ErrorKind::StorageFailure, db.last_error()});
}

}  // namespace

SqliteNoteStore::SqliteNoteStore(std::shared_ptr<SqliteDb> db)
    : db_(std::move(db)) {}

application::Result<domain::Note> SqliteNoteStore::load(
    const domain::NoteId& id) const {
  Stmt st(db_->handle(),
          "SELECT id,folder_id,title,body,created_at,modified_at,revision,"
          "pinned FROM notes WHERE id=?");
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
          "SELECT id,folder_id,title,modified_at,revision,pinned FROM notes "
          "WHERE folder_id=? ORDER BY pinned DESC, modified_at DESC");
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

application::Result<domain::Note> SqliteNoteStore::save(
    const domain::Note& note) {
  auto begin = db_->begin_immediate();
  if (!begin) {
    return application::Result<domain::Note>::fail(begin.error());
  }

  Stmt sel(db_->handle(), "SELECT revision FROM notes WHERE id=?");
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
    if (current_rev != note.revision) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::RevisionConflict, "CAS failed"});
    }
    stored.revision = note.revision + 1;
    Stmt upd(db_->handle(),
             "UPDATE notes SET folder_id=?, title=?, body=?, modified_at=?, "
             "revision=?, pinned=? WHERE id=? AND revision=?");
    if (!upd.valid()) {
      (void)db_->rollback();
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
    const auto body = encode_content(note.content);
    upd.bind_text(1, note.folder_id.value());
    upd.bind_text(2, note.title);
    upd.bind_blob(3, body);
    upd.bind_int64(4, note.modified_at_ms);
    upd.bind_int64(5, stored.revision);
    upd.bind_int64(6, note.pinned ? 1 : 0);
    upd.bind_text(7, note.id.value());
    upd.bind_int64(8, current_rev);
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
             "revision,pinned) VALUES(?,?,?,?,?,?,?,?)");
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
          "SELECT n.id,n.folder_id,n.title,n.modified_at,n.revision,n.pinned "
          "FROM notes n JOIN notes_search s ON s.note_id=n.id "
          "WHERE s.search_text LIKE ? "
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
