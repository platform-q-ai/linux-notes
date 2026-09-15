#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_content.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}

std::filesystem::path temp_db(const char* name) {
  const auto dir =
      std::filesystem::temp_directory_path() / "linux-notes-migration";
  std::filesystem::create_directories(dir);
  auto path = dir / name;
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(std::filesystem::path(path.string() + "-wal"), ec);
  std::filesystem::remove(std::filesystem::path(path.string() + "-shm"), ec);
  return path;
}

int schema_version(sqlite3* db) {
  sqlite3_stmt* st = nullptr;
  require(sqlite3_prepare_v2(
              db, "SELECT COALESCE(MAX(version),0) FROM schema_migrations", -1,
              &st, nullptr) == SQLITE_OK,
          "prep version");
  require(sqlite3_step(st) == SQLITE_ROW, "step version");
  const int v = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return v;
}

bool table_exists(sqlite3* db, const char* name) {
  sqlite3_stmt* st = nullptr;
  require(sqlite3_prepare_v2(
              db,
              "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1",
              -1, &st, nullptr) == SQLITE_OK,
          "prep exists");
  sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
  const int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_ROW;
}

std::int64_t count_sql(sqlite3* db, const char* sql) {
  sqlite3_stmt* st = nullptr;
  require(sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK, "prep count");
  require(sqlite3_step(st) == SQLITE_ROW, "step count");
  const auto n = sqlite3_column_int64(st, 0);
  sqlite3_finalize(st);
  return n;
}

// Prior-PR shape: folders+notes+v1 stamp, NO notes_search.
void build_legacy_v1_without_search(const std::filesystem::path& path) {
  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "open raw");
  char* err = nullptr;
  auto exec = [&](const char* sql) {
    require(sqlite3_exec(raw, sql, nullptr, nullptr, &err) == SQLITE_OK, err ? err : sql);
  };
  exec("PRAGMA foreign_keys=ON;");
  exec(
      "CREATE TABLE schema_migrations ("
      "  version INTEGER PRIMARY KEY,"
      "  applied_at INTEGER NOT NULL"
      ");");
  exec(
      "CREATE TABLE folders ("
      "  id TEXT PRIMARY KEY,"
      "  name TEXT NOT NULL,"
      "  parent_id TEXT,"
      "  sort_order INTEGER NOT NULL DEFAULT 0,"
      "  created_at INTEGER NOT NULL DEFAULT 0,"
      "  modified_at INTEGER NOT NULL DEFAULT 0"
      ");");
  exec(
      "CREATE TABLE notes ("
      "  id TEXT PRIMARY KEY,"
      "  folder_id TEXT NOT NULL,"
      "  title TEXT NOT NULL DEFAULT '',"
      "  body BLOB NOT NULL,"
      "  created_at INTEGER NOT NULL,"
      "  modified_at INTEGER NOT NULL,"
      "  revision INTEGER NOT NULL,"
      "  pinned INTEGER NOT NULL DEFAULT 0"
      ");");
  exec(
      "INSERT INTO folders(id,name,parent_id,sort_order,created_at,modified_at) "
      "VALUES('root','Notes',NULL,0,0,0);");
  exec(
      "INSERT INTO notes(id,folder_id,title,body,created_at,modified_at,revision,pinned) "
      "VALUES('legacy-1','root','LegacyTitle','plain body text',1,1,1,0);");
  exec(
      "INSERT INTO schema_migrations(version, applied_at) VALUES(1, 1);");
  require(!table_exists(raw, "notes_search"), "precondition: no search");
  require(schema_version(raw) == 1, "precondition: v1");
  sqlite3_close(raw);
}

bool column_exists(sqlite3* db, const char* table, const char* column) {
  std::string sql = std::string("PRAGMA table_info(") + table + ")";
  sqlite3_stmt* st = nullptr;
  require(sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK,
          "prep table_info");
  bool found = false;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const auto* name =
        reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    if (name && std::string(name) == column) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(st);
  return found;
}

void test_fresh_db_reaches_latest() {
  using namespace notes;
  const auto path = temp_db("fresh.db");
  adapters::persistence::SqliteDb db;
  require(static_cast<bool>(db.open(path)), "open fresh");
  require(schema_version(db.handle()) == 3, "latest version 3");
  require(table_exists(db.handle(), "notes_search"), "search present");
  require(table_exists(db.handle(), "folders"), "folders");
  require(column_exists(db.handle(), "notes", "trashed_at"), "trashed_at");
  require(column_exists(db.handle(), "notes", "trashed_from_folder_id"),
          "trashed_from");
  require(count_sql(db.handle(), "SELECT COUNT(*) FROM folders WHERE id='root'") == 1,
          "root seeded");
}

void test_legacy_v1_upgrades_and_backfills_search() {
  using namespace notes;
  const auto path = temp_db("legacy-v1.db");
  build_legacy_v1_without_search(path);

  adapters::persistence::SqliteDb db;
  auto opened = db.open(path);
  require(static_cast<bool>(opened),
          opened ? "open ok" : opened.error().message.c_str());
  require(schema_version(db.handle()) == 3, "upgraded to v3");
  require(table_exists(db.handle(), "notes_search"), "search created");
  require(column_exists(db.handle(), "notes", "trashed_at"), "trash col");
  require(count_sql(db.handle(), "SELECT COUNT(*) FROM notes") == 1,
          "note preserved");
  require(count_sql(db.handle(),
                    "SELECT COUNT(*) FROM notes WHERE id='legacy-1' AND trashed_at=0") ==
              1,
          "legacy note remains active");
  require(count_sql(db.handle(),
                    "SELECT COUNT(*) FROM notes_search WHERE note_id='legacy-1'") ==
              1,
          "search backfilled");
  // Index-only row count matches notes (no invented docs).
  require(count_sql(db.handle(), "SELECT COUNT(*) FROM notes_search") == 1,
          "search cardinality");
}

void test_legacy_v1_note_readable_and_searchable() {
  using namespace notes;
  const auto path = temp_db("legacy-v1-store.db");
  build_legacy_v1_without_search(path);

  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(path)), "open");
  adapters::persistence::SqliteNoteStore store(db);

  auto loaded = store.load(domain::NoteId{std::string{"legacy-1"}});
  require(loaded.has_value(), "load legacy note");
  require(loaded.value().title == "LegacyTitle", "title preserved");
  require(loaded.value().content.plain_text().find("plain body") != std::string::npos,
          "body preserved");

  auto hits = store.search("legacytitle");
  require(hits.has_value() && hits.value().size() == 1, "search works after migrate");
}

void test_search_orphan_index_row_dropped_note_kept() {
  using namespace notes;
  const auto path = temp_db("orphan-search.db");
  // Build full current schema then inject orphan search row + missing folder note.
  {
    adapters::persistence::SqliteDb db;
    require(static_cast<bool>(db.open(path)), "seed open");
  }
  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "reopen");
  char* err = nullptr;
  require(sqlite3_exec(raw,
                       "INSERT INTO notes_search(note_id, search_text) "
                       "VALUES('ghost-note','ghost');",
                       nullptr, nullptr, &err) == SQLITE_OK,
          err ? err : "insert ghost");
  require(count_sql(raw, "SELECT COUNT(*) FROM notes_search WHERE note_id='ghost-note'") ==
              1,
          "ghost present");
  sqlite3_close(raw);

  adapters::persistence::SqliteDb db2;
  require(static_cast<bool>(db2.open(path)), "reopen migrate");
  // Index-only orphan may be dropped; never invent a note for it.
  require(count_sql(db2.handle(),
                    "SELECT COUNT(*) FROM notes WHERE id='ghost-note'") == 0,
          "no invented note");
  require(count_sql(db2.handle(),
                    "SELECT COUNT(*) FROM notes_search WHERE note_id='ghost-note'") == 0,
          "ghost search removed");
}

void test_note_with_missing_folder_refused() {
  using namespace notes;
  const auto path = temp_db("orphan-note.db");
  build_legacy_v1_without_search(path);
  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "open");
  char* err = nullptr;
  require(sqlite3_exec(raw,
                       "INSERT INTO notes(id,folder_id,title,body,created_at,"
                       "modified_at,revision,pinned) "
                       "VALUES('orphan-n','missing-folder','x','y',1,1,1,0);",
                       nullptr, nullptr, &err) == SQLITE_OK,
          err ? err : "insert orphan note");
  sqlite3_close(raw);

  adapters::persistence::SqliteDb db;
  auto opened = db.open(path);
  require(!opened.has_value(), "must refuse ambiguous note orphan");
  require(opened.error().kind == application::ErrorKind::StorageFailure,
          "storage failure");
  require(opened.error().message.find("missing folders") != std::string::npos ||
              opened.error().message.find("ambiguous") != std::string::npos,
          "visible failure message");
}

void test_future_version_refused() {
  using namespace notes;
  const auto path = temp_db("future.db");
  {
    adapters::persistence::SqliteDb db;
    require(static_cast<bool>(db.open(path)), "seed");
  }
  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "open");
  char* err = nullptr;
  require(sqlite3_exec(raw,
                       "INSERT INTO schema_migrations(version, applied_at) "
                       "VALUES(999, 1);",
                       nullptr, nullptr, &err) == SQLITE_OK,
          err ? err : "stamp future");
  sqlite3_close(raw);

  adapters::persistence::SqliteDb db;
  auto opened = db.open(path);
  require(!opened.has_value(), "refuse future");
  require(opened.error().message.find("newer than supported") != std::string::npos,
          "message");
}

void test_partial_legacy_mismatch_refused() {
  using namespace notes;
  const auto path = temp_db("partial.db");
  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "open");
  char* err = nullptr;
  require(sqlite3_exec(raw,
                       "CREATE TABLE notes(id TEXT PRIMARY KEY);",
                       nullptr, nullptr, &err) == SQLITE_OK,
          err ? err : "notes only");
  sqlite3_close(raw);

  adapters::persistence::SqliteDb db;
  auto opened = db.open(path);
  require(!opened.has_value(), "refuse partial");
  require(opened.error().message.find("partial") != std::string::npos, "msg");
}

void test_idempotent_reopen() {
  using namespace notes;
  const auto path = temp_db("idem.db");
  {
    adapters::persistence::SqliteDb db;
    require(static_cast<bool>(db.open(path)), "first");
    require(schema_version(db.handle()) == 3, "v3");
  }
  adapters::persistence::SqliteDb db2;
  require(static_cast<bool>(db2.open(path)), "second");
  require(schema_version(db2.handle()) == 3, "still v3");
}

void test_v2_upgrades_to_v3_preserves_notes() {
  using namespace notes;
  const auto path = temp_db("v2-to-v3.db");
  // Build a stamped-v2 DB without trash columns (prior daily-driver shape).
  {
    adapters::persistence::SqliteDb db;
    require(static_cast<bool>(db.open(path)), "seed open to latest then rewind");
  }
  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "raw");
  char* err = nullptr;
  auto exec = [&](const char* sql) {
    require(sqlite3_exec(raw, sql, nullptr, nullptr, &err) == SQLITE_OK,
            err ? err : sql);
  };
  // Drop v3 stamp and columns if present to simulate pure v2.
  exec("DELETE FROM schema_migrations WHERE version>=3;");
  // Rebuild notes without trash columns; drop search first (FK → notes).
  exec("PRAGMA foreign_keys=OFF;");
  exec("DROP TABLE IF EXISTS notes_search;");
  exec("ALTER TABLE notes RENAME TO notes_old;");
  exec(
      "CREATE TABLE notes ("
      "  id TEXT PRIMARY KEY,"
      "  folder_id TEXT NOT NULL,"
      "  title TEXT NOT NULL DEFAULT '',"
      "  body BLOB NOT NULL,"
      "  created_at INTEGER NOT NULL,"
      "  modified_at INTEGER NOT NULL,"
      "  revision INTEGER NOT NULL,"
      "  pinned INTEGER NOT NULL DEFAULT 0,"
      "  FOREIGN KEY (folder_id) REFERENCES folders(id)"
      ");");
  exec(
      "INSERT INTO notes(id,folder_id,title,body,created_at,modified_at,revision,pinned) "
      "SELECT id,folder_id,title,body,created_at,modified_at,revision,pinned FROM notes_old;");
  exec("DROP TABLE notes_old;");
  exec(
      "INSERT INTO notes(id,folder_id,title,body,created_at,modified_at,revision,pinned) "
      "VALUES('keep-me','root','Keep','body',1,1,1,0);");
  // Recreate a v2-style search table without full rebuild of FTS; migrate will
  // ensure_notes_search_canonical.
  exec(
      "CREATE TABLE notes_search ("
      "  note_id TEXT PRIMARY KEY,"
      "  search_text TEXT NOT NULL,"
      "  FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE"
      ");");
  exec(
      "INSERT INTO notes_search(note_id, search_text) "
      "SELECT id, lower(title || ' ' || CAST(body AS TEXT)) FROM notes;");
  exec("INSERT OR IGNORE INTO schema_migrations(version, applied_at) VALUES(2, 1);");
  exec("PRAGMA foreign_keys=ON;");
  require(schema_version(raw) == 2, "stamped v2");
  require(!column_exists(raw, "notes", "trashed_at"), "no trash yet");
  sqlite3_close(raw);

  adapters::persistence::SqliteDb db;
  auto opened = db.open(path);
  require(static_cast<bool>(opened),
          opened ? "ok" : opened.error().message.c_str());
  require(schema_version(db.handle()) == 3, "now v3");
  require(column_exists(db.handle(), "notes", "trashed_at"), "trash added");
  require(count_sql(db.handle(), "SELECT COUNT(*) FROM notes WHERE id='keep-me'") == 1,
          "note survived");
  require(count_sql(db.handle(),
                    "SELECT COUNT(*) FROM notes WHERE id='keep-me' AND trashed_at=0") == 1,
          "active");
}

// Attachment identities live in note body blobs (filesystem store is external).
// Upgrade must not rewrite/lose ATT refs in structured content.
void test_v1_to_v3_preserves_attachment_ref_in_body() {
  using namespace notes;
  const auto path = temp_db("v1-att-ref.db");
  build_legacy_v1_without_search(path);
  // Overwrite legacy-1 body with structured content containing ATT id.
  {
    sqlite3* raw = nullptr;
    require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "raw");
    const std::string blob =
        "V1\nPARA\nSPAN 0 0 0 4:text\nEND\n"
        "ATT att-survive-1 9:photo.png\nEND\n"
        "CHECK\nITEM 0 4:todo\nEND\n";
    sqlite3_stmt* st = nullptr;
    require(sqlite3_prepare_v2(
                raw, "UPDATE notes SET body=? WHERE id='legacy-1'", -1, &st,
                nullptr) == SQLITE_OK,
            "prep body");
    sqlite3_bind_blob(st, 1, blob.data(), static_cast<int>(blob.size()),
                      SQLITE_TRANSIENT);
    require(sqlite3_step(st) == SQLITE_DONE, "upd body");
    sqlite3_finalize(st);
    sqlite3_close(raw);
  }

  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  auto opened = db->open(path);
  require(static_cast<bool>(opened),
          opened ? "ok" : opened.error().message.c_str());
  require(schema_version(db->handle()) == 3, "v3");
  adapters::persistence::SqliteNoteStore store(db);
  auto loaded = store.load(domain::NoteId{std::string{"legacy-1"}});
  require(loaded.has_value(), "load");
  bool att_ok = false;
  bool check_ok = false;
  for (const auto& b : loaded.value().content.blocks()) {
    if (const auto* a = std::get_if<domain::AttachmentRefBlock>(&b)) {
      att_ok = a->attachment_id.value() == "att-survive-1" &&
               a->display_name == "photo.png";
    }
    if (std::holds_alternative<domain::ChecklistBlock>(b)) check_ok = true;
  }
  require(att_ok, "attachment id survived migration");
  require(check_ok, "checklist survived migration");
  require(!loaded.value().is_trashed(), "still active");
}

// Refused opens must not advance schema_migrations on the source file.
void test_failed_migrate_leaves_prior_version_stamp() {
  using namespace notes;
  const auto path = temp_db("fail-stamp.db");
  {
    adapters::persistence::SqliteDb db;
    require(static_cast<bool>(db.open(path)), "seed v3");
    require(schema_version(db.handle()) == 3, "v3");
  }
  // Stamp a future version without changing tables.
  {
    sqlite3* raw = nullptr;
    require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "raw");
    char* err = nullptr;
    require(sqlite3_exec(raw,
                         "INSERT INTO schema_migrations(version, applied_at) "
                         "VALUES(999, 42);",
                         nullptr, nullptr, &err) == SQLITE_OK,
            err ? err : "future");
    require(schema_version(raw) == 999, "stamped future");
    // User note still present
    require(count_sql(raw, "SELECT COUNT(*) FROM notes") >= 0, "notes table");
    sqlite3_close(raw);
  }
  {
    adapters::persistence::SqliteDb db;
    auto opened = db.open(path);
    require(!opened.has_value(), "refuse future");
  }
  // On-disk stamp unchanged (no silent downgrade / rewrite).
  {
    sqlite3* raw = nullptr;
    require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "reopen raw");
    require(schema_version(raw) == 999, "stamp preserved after refuse");
    require(table_exists(raw, "notes"), "notes intact");
    require(table_exists(raw, "folders"), "folders intact");
    sqlite3_close(raw);
  }
}

// Mid-upgrade failure: force apply path to fail after begin by using partial
// legacy mismatch (folders without notes) — version stays 0, no half tables.
void test_partial_mismatch_does_not_stamp_version() {
  using namespace notes;
  const auto path = temp_db("partial-no-stamp.db");
  {
    sqlite3* raw = nullptr;
    require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "raw");
    char* err = nullptr;
    auto exec = [&](const char* sql) {
      require(sqlite3_exec(raw, sql, nullptr, nullptr, &err) == SQLITE_OK,
              err ? err : sql);
    };
    exec("CREATE TABLE folders (id TEXT PRIMARY KEY, name TEXT NOT NULL);");
    exec("INSERT INTO folders(id,name) VALUES('root','Notes');");
    // No schema_migrations, no notes → partial mismatch
    sqlite3_close(raw);
  }
  {
    adapters::persistence::SqliteDb db;
    auto opened = db.open(path);
    require(!opened.has_value(), "refused");
  }
  {
    sqlite3* raw = nullptr;
    require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "raw2");
    // migrations table may be created outside txn for bookkeeping, but no
    // successful version stamp for a completed schema.
    if (table_exists(raw, "schema_migrations")) {
      require(schema_version(raw) == 0, "no version stamped");
    }
    require(table_exists(raw, "folders"), "preexisting folder kept");
    require(!table_exists(raw, "notes"), "did not invent notes");
    sqlite3_close(raw);
  }
}

}  // namespace

int main() {
  try {
    test_fresh_db_reaches_latest();
    test_legacy_v1_upgrades_and_backfills_search();
    test_legacy_v1_note_readable_and_searchable();
    test_search_orphan_index_row_dropped_note_kept();
    test_note_with_missing_folder_refused();
    test_future_version_refused();
    test_partial_legacy_mismatch_refused();
    test_idempotent_reopen();
    test_v2_upgrades_to_v3_preserves_notes();
    test_v1_to_v3_preserves_attachment_ref_in_body();
    test_failed_migrate_leaves_prior_version_stamp();
    test_partial_mismatch_does_not_stamp_version();
    std::cerr << "migration_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "migration_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}

