#include "adapters/persistence/sqlite/sqlite_db.hpp"

#include <sqlite3.h>

#include <string>

namespace notes::adapters::persistence {
namespace {

// Latest durable schema version stamped in schema_migrations after successful
// upgrade. v1 = base tables; v2 = notes_search + integrity backfill (repairs
// prior-PR DBs that stamped v1 without a usable search index / FK).
constexpr int kLatestSchemaVersion = 2;

constexpr const char* kEnsureMigrationsTable = R"SQL(
CREATE TABLE IF NOT EXISTS schema_migrations (
  version INTEGER PRIMARY KEY,
  applied_at INTEGER NOT NULL
);
)SQL";

constexpr const char* kCreateFolders = R"SQL(
CREATE TABLE IF NOT EXISTS folders (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  parent_id TEXT,
  sort_order INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL DEFAULT 0,
  modified_at INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY (parent_id) REFERENCES folders(id)
);
)SQL";

constexpr const char* kCreateNotes = R"SQL(
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
)SQL";

constexpr const char* kCreateNoteIndexes = R"SQL(
CREATE INDEX IF NOT EXISTS idx_notes_folder ON notes(folder_id);
CREATE INDEX IF NOT EXISTS idx_notes_modified ON notes(modified_at DESC);
)SQL";

constexpr const char* kCreateNotesSearch = R"SQL(
CREATE TABLE IF NOT EXISTS notes_search (
  note_id TEXT PRIMARY KEY,
  search_text TEXT NOT NULL,
  FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE
);
)SQL";

application::Result<void> fail_msg(std::string msg) {
  return application::Result<void>::fail(
      {application::ErrorKind::StorageFailure, std::move(msg)});
}

bool table_exists(sqlite3* db, const char* name) {
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_ROW;
}

bool column_exists(sqlite3* db, const char* table, const char* column) {
  // PRAGMA table_info does not bind; validate identifiers are simple.
  for (const char* p = table; *p; ++p) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
      return false;
    }
  }
  std::string sql = std::string("PRAGMA table_info(") + table + ")";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  bool found = false;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const auto* name =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (name && column && std::string(name) == column) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(stmt);
  return found;
}

int notes_search_fk_count(sqlite3* db) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_list(notes_search)", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    return -1;
  }
  int n = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ++n;
  }
  sqlite3_finalize(stmt);
  return n;
}

application::Result<int> read_schema_version(sqlite3* db) {
  if (!table_exists(db, "schema_migrations")) {
    return application::Result<int>::ok(0);
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(version),0) FROM schema_migrations",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    return application::Result<int>::fail(
        {application::ErrorKind::StorageFailure, sqlite3_errmsg(db)});
  }
  int version = 0;
  const int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    version = static_cast<int>(sqlite3_column_int64(stmt, 0));
  } else if (rc != SQLITE_DONE) {
    const std::string msg = sqlite3_errmsg(db);
    sqlite3_finalize(stmt);
    return application::Result<int>::fail(
        {application::ErrorKind::StorageFailure, msg});
  }
  sqlite3_finalize(stmt);
  return application::Result<int>::ok(version);
}

application::Result<void> stamp_version(SqliteDb& self, int version) {
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
      "VALUES(?, strftime('%s','now'))";
  if (sqlite3_prepare_v2(self.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return fail_msg(self.last_error());
  }
  sqlite3_bind_int(stmt, 1, version);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return fail_msg(self.last_error());
  }
  return application::Result<void>::ok();
}

application::Result<void> seed_root_folder(SqliteDb& self) {
  return self.exec(
      "INSERT OR IGNORE INTO folders(id,name,parent_id,sort_order,created_at,"
      "modified_at) VALUES('root','Notes',NULL,0,0,0);");
}

application::Result<std::int64_t> query_int64(SqliteDb& self, const char* sql) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(self.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return application::Result<std::int64_t>::fail(
        {application::ErrorKind::StorageFailure, self.last_error()});
  }
  std::int64_t value = 0;
  const int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    value = sqlite3_column_int64(stmt, 0);
  } else if (rc != SQLITE_DONE) {
    const std::string msg = self.last_error();
    sqlite3_finalize(stmt);
    return application::Result<std::int64_t>::fail(
        {application::ErrorKind::StorageFailure, msg});
  }
  sqlite3_finalize(stmt);
  return application::Result<std::int64_t>::ok(value);
}

// Derive search_text the same way as SqliteNoteStore (title + body lowercased).
// Body is stored as a length-safe blob; for backfill we use a SQL lower(title||' '||
// coalesce(body,'')) approximation that matches plain-text notes and is lossless
// for indexing purposes without inventing note content.
application::Result<void> backfill_notes_search(SqliteDb& self) {
  // Insert missing search rows from notes. Do not invent note bodies.
  auto e = self.exec(
      "INSERT INTO notes_search(note_id, search_text) "
      "SELECT n.id, lower(n.title || ' ' || coalesce(CAST(n.body AS TEXT), '')) "
      "FROM notes n "
      "WHERE NOT EXISTS (SELECT 1 FROM notes_search s WHERE s.note_id = n.id);");
  if (!e) return e;

  // Index-only orphans (search row without a note) are not user documents.
  // Drop them so FK integrity holds; never DELETE from notes here.
  e = self.exec(
      "DELETE FROM notes_search WHERE note_id NOT IN (SELECT id FROM notes);");
  if (!e) return e;
  return application::Result<void>::ok();
}

application::Result<void> ensure_notes_search_canonical(SqliteDb& self) {
  sqlite3* db = self.handle();
  if (!table_exists(db, "notes_search")) {
    auto e = self.exec(kCreateNotesSearch);
    if (!e) return e;
    return backfill_notes_search(self);
  }

  // Table exists: require expected columns.
  if (!column_exists(db, "notes_search", "note_id") ||
      !column_exists(db, "notes_search", "search_text")) {
    return fail_msg(
        "migration failed: notes_search has incompatible columns; "
        "refusing automatic rebuild to avoid data loss");
  }

  // If FK to notes is missing (legacy CREATE without FK), rebuild table shape
  // inside the caller's transaction. Copy only rows that still reference notes.
  if (notes_search_fk_count(db) <= 0) {
    auto e = self.exec(
        "CREATE TABLE notes_search_v2 ("
        "  note_id TEXT PRIMARY KEY,"
        "  search_text TEXT NOT NULL,"
        "  FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE"
        ");");
    if (!e) return e;
    e = self.exec(
        "INSERT INTO notes_search_v2(note_id, search_text) "
        "SELECT s.note_id, s.search_text FROM notes_search s "
        "INNER JOIN notes n ON n.id = s.note_id;");
    if (!e) return e;
    e = self.exec("DROP TABLE notes_search;");
    if (!e) return e;
    e = self.exec("ALTER TABLE notes_search_v2 RENAME TO notes_search;");
    if (!e) return e;
  }

  return backfill_notes_search(self);
}

application::Result<void> verify_base_tables(SqliteDb& self) {
  sqlite3* db = self.handle();
  if (!table_exists(db, "folders") || !table_exists(db, "notes")) {
    return fail_msg(
        "migration failed: required tables folders/notes missing after upgrade");
  }
  if (!column_exists(db, "notes", "body") ||
      !column_exists(db, "notes", "revision") ||
      !column_exists(db, "notes", "folder_id")) {
    return fail_msg(
        "migration failed: notes table missing required columns "
        "(incompatible legacy schema)");
  }
  if (!table_exists(db, "notes_search")) {
    return fail_msg("migration failed: notes_search missing after upgrade");
  }

  // Ambiguous user-data integrity: notes pointing at missing folders.
  auto orphan_notes = query_int64(
      self,
      "SELECT COUNT(*) FROM notes n "
      "WHERE n.folder_id IS NOT NULL "
      "AND n.folder_id NOT IN (SELECT id FROM folders)");
  if (!orphan_notes) return application::Result<void>::fail(orphan_notes.error());
  if (orphan_notes.value() > 0) {
    return fail_msg(
        "migration failed: notes reference missing folders "
        "(ambiguous orphans; manual recovery required)");
  }

  // Every note must have a search row after backfill.
  auto missing_search = query_int64(
      self,
      "SELECT COUNT(*) FROM notes n "
      "WHERE NOT EXISTS (SELECT 1 FROM notes_search s WHERE s.note_id=n.id)");
  if (!missing_search) {
    return application::Result<void>::fail(missing_search.error());
  }
  if (missing_search.value() > 0) {
    return fail_msg(
        "migration failed: notes_search backfill incomplete");
  }
  return application::Result<void>::ok();
}

application::Result<void> apply_v1_base(SqliteDb& self) {
  auto e = self.exec(kCreateFolders);
  if (!e) return e;
  e = self.exec(kCreateNotes);
  if (!e) return e;
  e = self.exec(kCreateNoteIndexes);
  if (!e) return e;
  e = seed_root_folder(self);
  if (!e) return e;
  return stamp_version(self, 1);
}

application::Result<void> apply_v2_search(SqliteDb& self) {
  auto e = ensure_notes_search_canonical(self);
  if (!e) return e;
  e = verify_base_tables(self);
  if (!e) return e;
  return stamp_version(self, 2);
}

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
  // Ensure migrations bookkeeping exists outside version txns so we can read it.
  auto e = exec(kEnsureMigrationsTable);
  if (!e) return e;

  auto version = read_schema_version(db_);
  if (!version) {
    return application::Result<void>::fail(version.error());
  }
  int current = version.value();

  if (current > kLatestSchemaVersion) {
    return fail_msg(
        "migration failed: database schema version " + std::to_string(current) +
        " is newer than supported version " +
        std::to_string(kLatestSchemaVersion));
  }

  // Detect pre-migration leftovers: user tables without a version stamp.
  if (current == 0) {
    const bool has_notes = table_exists(db_, "notes");
    const bool has_folders = table_exists(db_, "folders");
    if (has_notes != has_folders) {
      return fail_msg(
          "migration failed: partial legacy schema (folders/notes mismatch); "
          "refusing to invent or destroy data");
    }
  }

  // Apply versioned upgrades transactionally. Each step is atomic: failure
  // rolls back and surfaces StorageFailure (open fails — no silent half-state).
  while (current < kLatestSchemaVersion) {
    e = begin_immediate();
    if (!e) return e;

    if (current == 0) {
      e = apply_v1_base(*this);
    } else if (current == 1) {
      // Prior-PR path: may already have v1 stamp without notes_search/FKs.
      e = apply_v2_search(*this);
    } else {
      (void)rollback();
      return fail_msg("migration failed: no upgrade path from version " +
                      std::to_string(current));
    }

    if (!e) {
      (void)rollback();
      return e;
    }

    auto c = commit();
    if (!c) {
      (void)rollback();
      return c;
    }

    auto v2 = read_schema_version(db_);
    if (!v2) return application::Result<void>::fail(v2.error());
    if (v2.value() <= current) {
      return fail_msg(
          "migration failed: schema version did not advance (stuck at " +
          std::to_string(current) + ")");
    }
    current = v2.value();
  }

  // Already at latest: still verify integrity so a partially repaired file
  // cannot open silently broken (e.g. stamped v2 but search dropped later).
  if (current >= kLatestSchemaVersion) {
    e = begin_immediate();
    if (!e) return e;
    e = ensure_notes_search_canonical(*this);
    if (!e) {
      (void)rollback();
      return e;
    }
    e = verify_base_tables(*this);
    if (!e) {
      (void)rollback();
      return e;
    }
    e = commit();
    if (!e) {
      (void)rollback();
      return e;
    }
  }

  return seed_root_folder(*this);
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
