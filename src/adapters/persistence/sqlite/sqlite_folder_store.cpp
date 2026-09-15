#include "adapters/persistence/sqlite/sqlite_folder_store.hpp"

#include <sqlite3.h>

namespace notes::adapters::persistence {

SqliteFolderStore::SqliteFolderStore(std::shared_ptr<SqliteDb> db)
    : db_(std::move(db)) {}

application::Result<domain::Folder> SqliteFolderStore::load(
    const domain::FolderId& id) const {
  Stmt st(db_->handle(),
          "SELECT id,name,parent_id,sort_order,created_at,modified_at "
          "FROM folders WHERE id=?");
  if (!st.valid()) {
    return application::Result<domain::Folder>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  st.bind_text(1, id.value());
  const int rc = st.step();
  if (rc == SQLITE_DONE) {
    return application::Result<domain::Folder>::fail(
        {application::ErrorKind::NotFound, "folder not found"});
  }
  if (rc != SQLITE_ROW) {
    return application::Result<domain::Folder>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  domain::Folder f;
  f.id = domain::FolderId{st.column_text(0)};
  f.name = st.column_text(1);
  if (!st.column_is_null(2)) {
    f.parent_id = domain::FolderId{st.column_text(2)};
  }
  f.sort_order = st.column_int64(3);
  f.created_at_ms = st.column_int64(4);
  f.modified_at_ms = st.column_int64(5);
  return application::Result<domain::Folder>::ok(std::move(f));
}

application::Result<std::vector<domain::Folder>>
SqliteFolderStore::list_all() const {
  Stmt st(db_->handle(),
          "SELECT id,name,parent_id,sort_order,created_at,modified_at "
          "FROM folders ORDER BY sort_order ASC, name ASC");
  if (!st.valid()) {
    return application::Result<std::vector<domain::Folder>>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  std::vector<domain::Folder> out;
  while (true) {
    const int rc = st.step();
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) {
      return application::Result<std::vector<domain::Folder>>::fail(
          {application::ErrorKind::StorageFailure, db_->last_error()});
    }
    domain::Folder f;
    f.id = domain::FolderId{st.column_text(0)};
    f.name = st.column_text(1);
    if (!st.column_is_null(2)) {
      f.parent_id = domain::FolderId{st.column_text(2)};
    }
    f.sort_order = st.column_int64(3);
    f.created_at_ms = st.column_int64(4);
    f.modified_at_ms = st.column_int64(5);
    out.push_back(std::move(f));
  }
  return application::Result<std::vector<domain::Folder>>::ok(std::move(out));
}

application::Result<domain::Folder> SqliteFolderStore::save(
    const domain::Folder& folder) {
  auto begin = db_->begin_immediate();
  if (!begin) {
    return application::Result<domain::Folder>::fail(begin.error());
  }
  Stmt st(db_->handle(),
          "INSERT INTO folders(id,name,parent_id,sort_order,created_at,"
          "modified_at) VALUES(?,?,?,?,?,?) "
          "ON CONFLICT(id) DO UPDATE SET name=excluded.name, "
          "parent_id=excluded.parent_id, sort_order=excluded.sort_order, "
          "modified_at=excluded.modified_at");
  if (!st.valid()) {
    (void)db_->rollback();
    return application::Result<domain::Folder>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  st.bind_text(1, folder.id.value());
  st.bind_text(2, folder.name);
  if (folder.parent_id) {
    st.bind_text(3, folder.parent_id->value());
  } else {
    st.bind_null(3);
  }
  st.bind_int64(4, folder.sort_order);
  st.bind_int64(5, folder.created_at_ms);
  st.bind_int64(6, folder.modified_at_ms);
  if (st.step() != SQLITE_DONE) {
    (void)db_->rollback();
    return application::Result<domain::Folder>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  auto c = db_->commit();
  if (!c) {
    return application::Result<domain::Folder>::fail(c.error());
  }
  return application::Result<domain::Folder>::ok(folder);
}

application::Result<void> SqliteFolderStore::remove(
    const domain::FolderId& id) {
  if (id.value() == "root") {
    return application::Result<void>::fail(
        {application::ErrorKind::ValidationFailed, "cannot remove root"});
  }
  auto begin = db_->begin_immediate();
  if (!begin) return begin;

  Stmt child_folders(
      db_->handle(),
      "SELECT 1 FROM folders WHERE parent_id=? LIMIT 1");
  if (!child_folders.valid()) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  child_folders.bind_text(1, id.value());
  if (child_folders.step() == SQLITE_ROW) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::ValidationFailed,
         "folder has child folders; move or delete them first"});
  }

  Stmt child_notes(db_->handle(),
                   "SELECT 1 FROM notes WHERE folder_id=? LIMIT 1");
  if (!child_notes.valid()) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  child_notes.bind_text(1, id.value());
  if (child_notes.step() == SQLITE_ROW) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::ValidationFailed,
         "folder still contains notes; move or delete them first"});
  }

  Stmt st(db_->handle(), "DELETE FROM folders WHERE id=?");
  if (!st.valid()) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  st.bind_text(1, id.value());
  if (st.step() != SQLITE_DONE) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::StorageFailure, db_->last_error()});
  }
  if (sqlite3_changes(db_->handle()) == 0) {
    (void)db_->rollback();
    return application::Result<void>::fail(
        {application::ErrorKind::NotFound, "folder not found"});
  }
  return db_->commit();
}

}  // namespace notes::adapters::persistence
