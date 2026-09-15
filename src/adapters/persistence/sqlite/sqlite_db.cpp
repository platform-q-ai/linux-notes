#include "adapters/persistence/sqlite/sqlite_db.hpp"

#include <sqlite3.h>

namespace notes::adapters::persistence {
namespace {

constexpr const char* kSchemaV1 = R"SQL(
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS schema_migrations (
  version INTEGER PRIMARY KEY,
  applied_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS folders (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  parent_id TEXT,
  sort_order INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL DEFAULT 0,
  modified_at INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY (parent_id) REFERENCES folders(id)
);
CREATE TABLE IF NOT EXISTS notes (
  id TEXT PRIMARY KEY,
  folder_id TEXT NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  body BLOB NOT NULL,
  created_at INTEGER NOT NULL,
  modified_at INTEGER NOT NULL,
  revision INTEGER NOT NULL,
  pinned INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY (folder_id) REFERENCES folders(id)
);
CREATE INDEX IF NOT EXISTS idx_notes_folder ON notes(folder_id);
CREATE INDEX IF NOT EXISTS idx_notes_modified ON notes(modified_at DESC);
CREATE TABLE IF NOT EXISTS notes_search (
  note_id TEXT PRIMARY KEY,
  search_text TEXT NOT NULL,
  FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE
);
)SQL";

}  // namespace

SqliteDb::~SqliteDb() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

SqliteDb::SqliteDb(SqliteDb&& other) noexcept : db_(other.db_) {
  other.db_ = nullptr;
}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept {
  if (this != &other) {
    if (db_ != nullptr) sqlite3_close(db_);
    db_ = other.db_;
    other.db_ = nullptr;
  }
  return *this;
}

application::Result<void> SqliteDb::open(const std::filesystem::path& path) {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  sqlite3* handle = nullptr;
  const int rc = sqlite3_open(path.string().c_str(), &handle);
  if (rc != SQLITE_OK) {
    std::string msg = handle ? sqlite3_errmsg(handle) : "sqlite3_open failed";
    if (handle) sqlite3_close(handle);
    return application::Result<void>::fail(
        {application::ErrorKind::StorageFailure, std::move(msg)});
  }
  db_ = handle;
  auto e = exec("PRAGMA journal_mode=WAL;");
  if (!e) return e;
  e = exec("PRAGMA synchronous=NORMAL;");
  if (!e) return e;
  e = exec("PRAGMA foreign_keys=ON;");
  if (!e) return e;
  return migrate();
}

application::Result<void> SqliteDb::exec(const std::string& sql) {
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err ? err : "exec failed";
    sqlite3_free(err);
    return application::Result<void>::fail(
        {application::ErrorKind::StorageFailure, std::move(msg)});
  }
  return application::Result<void>::ok();
}

application::Result<void> SqliteDb::migrate() {
  auto e = exec(kSchemaV1);
  if (!e) return e;
  e = exec(
      "INSERT OR IGNORE INTO folders(id,name,parent_id,sort_order,created_at,"
      "modified_at) VALUES('root','Notes',NULL,0,0,0);");
  if (!e) return e;
  return exec(
      "INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
      "VALUES(1, strftime('%s','now'));");
}

application::Result<void> SqliteDb::begin_immediate() {
  return exec("BEGIN IMMEDIATE;");
}
application::Result<void> SqliteDb::commit() { return exec("COMMIT;"); }
application::Result<void> SqliteDb::rollback() { return exec("ROLLBACK;"); }

std::string SqliteDb::last_error() const {
  return db_ ? sqlite3_errmsg(db_) : "db closed";
}

Stmt::Stmt(sqlite3* db, const char* sql) {
  if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
    stmt_ = nullptr;
  }
}
Stmt::~Stmt() {
  if (stmt_) sqlite3_finalize(stmt_);
}
Stmt::Stmt(Stmt&& o) noexcept : stmt_(o.stmt_) { o.stmt_ = nullptr; }
Stmt& Stmt::operator=(Stmt&& o) noexcept {
  if (this != &o) {
    if (stmt_) sqlite3_finalize(stmt_);
    stmt_ = o.stmt_;
    o.stmt_ = nullptr;
  }
  return *this;
}
void Stmt::reset() {
  if (stmt_) {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }
}
void Stmt::bind_text(int idx, const std::string& v) {
  sqlite3_bind_text(stmt_, idx, v.c_str(), static_cast<int>(v.size()),
                    SQLITE_TRANSIENT);
}
void Stmt::bind_int64(int idx, std::int64_t v) {
  sqlite3_bind_int64(stmt_, idx, v);
}
void Stmt::bind_blob(int idx, const std::string& v) {
  sqlite3_bind_blob(stmt_, idx, v.data(), static_cast<int>(v.size()),
                    SQLITE_TRANSIENT);
}
void Stmt::bind_null(int idx) { sqlite3_bind_null(stmt_, idx); }
int Stmt::step() { return sqlite3_step(stmt_); }
std::string Stmt::column_text(int idx) const {
  const auto* p =
      reinterpret_cast<const char*>(sqlite3_column_text(stmt_, idx));
  if (!p) return {};
  return std::string(
      p, static_cast<std::size_t>(sqlite3_column_bytes(stmt_, idx)));
}
std::int64_t Stmt::column_int64(int idx) const {
  return sqlite3_column_int64(stmt_, idx);
}
bool Stmt::column_is_null(int idx) const {
  return sqlite3_column_type(stmt_, idx) == SQLITE_NULL;
}

}  // namespace notes::adapters::persistence
