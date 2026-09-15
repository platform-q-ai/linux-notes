// Expanded migration integrity for schema v3 trash columns + failure cases.
// Complements tests/adapters/migration_test.cpp (owned by trash-org during task4).

#include "adapters/persistence/sqlite/content_codec.hpp"
#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "tests/support/require.hpp"
#include "tests/support/structured_fixtures.hpp"
#include "tests/support/temp_db.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

using notes::testing::require;

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

bool column_exists(sqlite3* db, const char* table, const char* column) {
  std::string sql = std::string("PRAGMA table_info(") + table + ")";
  sqlite3_stmt* st = nullptr;
  require(sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK,
          "pragma");
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

void build_v2_with_structured_note(const std::filesystem::path& path) {
  // Produce a DB stamped at v2 without trash columns (pre-v3 shape).
  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "open");
  char* err = nullptr;
  auto exec = [&](const char* sql) {
    require(sqlite3_exec(raw, sql, nullptr, nullptr, &err) == SQLITE_OK,
            err ? err : sql);
  };
  exec("PRAGMA foreign_keys=ON;");
  exec(
      "CREATE TABLE schema_migrations ("
      "  version INTEGER PRIMARY KEY, applied_at INTEGER NOT NULL);");
  exec(
      "CREATE TABLE folders ("
      "  id TEXT PRIMARY KEY, name TEXT NOT NULL, parent_id TEXT,"
      "  sort_order INTEGER NOT NULL DEFAULT 0,"
      "  created_at INTEGER NOT NULL DEFAULT 0,"
      "  modified_at INTEGER NOT NULL DEFAULT 0);");
  exec(
      "CREATE TABLE notes ("
      "  id TEXT PRIMARY KEY, folder_id TEXT NOT NULL,"
      "  title TEXT NOT NULL DEFAULT '', body BLOB NOT NULL,"
      "  created_at INTEGER NOT NULL, modified_at INTEGER NOT NULL,"
      "  revision INTEGER NOT NULL, pinned INTEGER NOT NULL DEFAULT 0);");
  exec(
      "CREATE TABLE notes_search ("
      "  note_id TEXT PRIMARY KEY, search_text TEXT NOT NULL,"
      "  FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE);");
  exec(
      "INSERT INTO folders(id,name,parent_id,sort_order,created_at,modified_at) "
      "VALUES('root','Notes',NULL,0,0,0);");
  // Minimal structured body via plain text is fine for survival; also a blob.
  const auto content = notes::testing::make_mixed_structured_content();
  const auto blob = notes::adapters::persistence::encode_content(content);
  {
    sqlite3_stmt* st = nullptr;
    require(sqlite3_prepare_v2(
                raw,
                "INSERT INTO notes(id,folder_id,title,body,created_at,"
                "modified_at,revision,pinned) VALUES(?,?,?,?,1,1,1,0)",
                -1, &st, nullptr) == SQLITE_OK,
            "prep note");
    const std::string id = "v2-struct";
    const std::string title = "KeepMe";
    const std::string folder = "root";
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, folder.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st, 4, blob.data(), static_cast<int>(blob.size()),
                      SQLITE_TRANSIENT);
    require(sqlite3_step(st) == SQLITE_DONE, "ins note");
    sqlite3_finalize(st);
  }
  exec(
      "INSERT INTO notes_search(note_id, search_text) "
      "VALUES('v2-struct','keepme structured');");
  exec("INSERT INTO schema_migrations(version, applied_at) VALUES(1,1);");
  exec("INSERT INTO schema_migrations(version, applied_at) VALUES(2,1);");
  require(schema_version(raw) == 2, "pre v2");
  require(!column_exists(raw, "notes", "trashed_at"), "no trash col yet");
  sqlite3_close(raw);
}

void test_fresh_reaches_v3_with_trash_columns() {
  notes::testing::TempDbPath path("mig-fresh-v3.db");
  notes::adapters::persistence::SqliteDb db;
  require(static_cast<bool>(db.open(path.path())), "open fresh");
  require(schema_version(db.handle()) == 3, "latest is 3");
  require(column_exists(db.handle(), "notes", "trashed_at"), "trashed_at");
  require(column_exists(db.handle(), "notes", "trashed_from_folder_id"),
          "trashed_from");
}

void test_v2_upgrades_preserves_structured_note() {
  notes::testing::TempDbPath path("mig-v2-to-v3.db");
  build_v2_with_structured_note(path.path());

  auto db = std::make_shared<notes::adapters::persistence::SqliteDb>();
  auto opened = db->open(path.path());
  require(static_cast<bool>(opened),
          opened ? "ok" : opened.error().message.c_str());
  require(schema_version(db->handle()) == 3, "upgraded to 3");
  require(column_exists(db->handle(), "notes", "trashed_at"), "col added");

  notes::adapters::persistence::SqliteNoteStore store(db);
  auto loaded = store.load(notes::domain::NoteId{std::string{"v2-struct"}});
  require(loaded.has_value(), "note survived");
  require(loaded.value().title == "KeepMe", "title");
  require(!loaded.value().is_trashed(), "active after migrate");
  require(notes::testing::content_has_checklist_and_attachment(loaded.value().content),
          "structured body survived");
}

void test_future_version_still_refused() {
  notes::testing::TempDbPath path("mig-future-v3.db");
  {
    notes::adapters::persistence::SqliteDb db;
    require(static_cast<bool>(db.open(path.path())), "seed");
  }
  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK, "open");
  char* err = nullptr;
  require(sqlite3_exec(raw,
                       "INSERT INTO schema_migrations(version, applied_at) "
                       "VALUES(999, 1);",
                       nullptr, nullptr, &err) == SQLITE_OK,
          err ? err : "future");
  sqlite3_close(raw);

  notes::adapters::persistence::SqliteDb db;
  auto opened = db.open(path.path());
  require(!opened.has_value(), "refuse future");
  require(opened.error().message.find("newer than supported") != std::string::npos,
          "msg");
}

void test_idempotent_v3_reopen() {
  notes::testing::TempDbPath path("mig-idem-v3.db");
  {
    notes::adapters::persistence::SqliteDb db;
    require(static_cast<bool>(db.open(path.path())), "first");
    require(schema_version(db.handle()) == 3, "v3");
  }
  notes::adapters::persistence::SqliteDb db2;
  require(static_cast<bool>(db2.open(path.path())), "second");
  require(schema_version(db2.handle()) == 3, "still v3");
}

void test_failed_open_leaves_prior_file_readable() {
  // Simulate: copy a good v3 DB, then a bad open attempt must not wipe data.
  notes::testing::TempDbPath good("mig-good.db");
  {
    auto db = std::make_shared<notes::adapters::persistence::SqliteDb>();
    require(static_cast<bool>(db->open(good.path())), "good open");
    notes::adapters::persistence::SqliteNoteStore store(db);
    auto n = notes::testing::make_structured_note("safe", "root");
    require(store.save(n).has_value(), "save");
  }
  // Corrupt by stamping future version on a copy path
  notes::testing::TempDbPath bad("mig-bad-future.db");
  std::filesystem::copy_file(good.path(), bad.path(),
                             std::filesystem::copy_options::overwrite_existing);
  {
    sqlite3* raw = nullptr;
    require(sqlite3_open(bad.string().c_str(), &raw) == SQLITE_OK, "open copy");
    char* err = nullptr;
    require(sqlite3_exec(raw,
                         "INSERT INTO schema_migrations(version, applied_at) "
                         "VALUES(999,1);",
                         nullptr, nullptr, &err) == SQLITE_OK,
            err ? err : "stamp");
    sqlite3_close(raw);
  }
  {
    notes::adapters::persistence::SqliteDb db;
    require(!db.open(bad.path()).has_value(), "bad refuses");
  }
  // Original good file still opens and has the note
  {
    auto db = std::make_shared<notes::adapters::persistence::SqliteDb>();
    require(static_cast<bool>(db->open(good.path())), "good still opens");
    notes::adapters::persistence::SqliteNoteStore store(db);
    auto loaded = store.load(notes::domain::NoteId{std::string{"safe"}});
    require(loaded.has_value(), "data intact on good file");
  }
}

}  // namespace

int main() {
  try {
    test_fresh_reaches_v3_with_trash_columns();
    test_v2_upgrades_preserves_structured_note();
    test_future_version_still_refused();
    test_idempotent_v3_reopen();
    test_failed_open_leaves_prior_file_readable();
    std::cerr << "migration_v3_integrity_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "migration_v3_integrity_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
