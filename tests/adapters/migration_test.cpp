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

void test_fresh_db_reaches_latest() {
  using namespace notes;
  const auto path = temp_db("fresh.db");
  adapters::persistence::SqliteDb db;
  require(static_cast<bool>(db.open(path)), "open fresh");
  require(schema_version(db.handle()) == 2, "latest version 2");
  require(table_exists(db.handle(), "notes_search"), "search present");
  require(table_exists(db.handle(), "folders"), "folders");
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
  require(schema_version(db.handle()) == 2, "upgraded to v2");
  require(table_exists(db.handle(), "notes_search"), "search created");
  require(count_sql(db.handle(), "SELECT COUNT(*) FROM notes") == 1,
          "note preserved");
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
    require(schema_version(db.handle()) == 2, "v2");
  }
  adapters::persistence::SqliteDb db2;
  require(static_cast<bool>(db2.open(path)), "second");
  require(schema_version(db2.handle()) == 2, "still v2");
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
    std::cerr << "migration_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "migration_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
