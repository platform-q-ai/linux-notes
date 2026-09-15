#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_content.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}

// Happy path still maintains search index.
void test_search_row_present_after_save() {
  using namespace notes;
  const auto dir =
      std::filesystem::temp_directory_path() / "linux-notes-search-ok";
  std::filesystem::create_directories(dir);
  const auto db_path = dir / "ok.db";
  std::error_code ec;
  std::filesystem::remove(db_path, ec);

  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(db_path)), "open");
  adapters::persistence::SqliteNoteStore store(db);

  domain::Note n;
  n.id = domain::NoteId{std::string{"note-search-1"}};
  n.folder_id = domain::FolderId{std::string{"root"}};
  n.title = "UniqueTokenZebra";
  n.content = domain::NoteContent::from_plain_text("body");
  n.created_at_ms = 1;
  n.modified_at_ms = 1;
  n.revision = 0;
  require(static_cast<bool>(store.save(n)), "save");

  auto hits = store.search("uniquetokenzebra");
  require(hits.has_value() && hits.value().size() == 1, "search hit");

  // Count search rows via exec probe using load path already validated.
  auto rm = store.remove(n.id);
  require(rm.has_value(), "remove");
  hits = store.search("uniquetokenzebra");
  require(hits.has_value() && hits.value().empty(), "gone from search");
}

// If notes_search is unavailable (renamed), save must fail closed and roll back.
void test_save_fails_closed_when_search_table_missing() {
  using namespace notes;
  const auto dir =
      std::filesystem::temp_directory_path() / "linux-notes-search-fail";
  std::filesystem::create_directories(dir);
  const auto db_path = dir / "fail.db";
  std::error_code ec;
  std::filesystem::remove(db_path, ec);

  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(db_path)), "open");

  // Sabotage search table after migrate — simulates prepare failure path.
  require(static_cast<bool>(db->exec("ALTER TABLE notes_search RENAME TO notes_search_broken;")),
          "rename search table");

  adapters::persistence::SqliteNoteStore store(db);
  domain::Note n;
  n.id = domain::NoteId{std::string{"note-should-not-commit"}};
  n.folder_id = domain::FolderId{std::string{"root"}};
  n.title = "Hidden";
  n.content = domain::NoteContent::from_plain_text("should rollback");
  n.created_at_ms = 1;
  n.modified_at_ms = 1;
  n.revision = 0;

  auto saved = store.save(n);
  require(!saved.has_value(), "save must fail when search maintain fails");
  require(saved.error().kind == application::ErrorKind::StorageFailure,
          "storage failure");

  // Note must not be durable without search row.
  auto loaded = store.load(n.id);
  require(!loaded.has_value() &&
              loaded.error().kind == application::ErrorKind::NotFound,
          "note rolled back");
}

// Update path also fail-closed.
void test_update_fails_closed_when_search_broken() {
  using namespace notes;
  const auto dir =
      std::filesystem::temp_directory_path() / "linux-notes-search-upd";
  std::filesystem::create_directories(dir);
  const auto db_path = dir / "upd.db";
  std::error_code ec;
  std::filesystem::remove(db_path, ec);

  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(db_path)), "open");
  adapters::persistence::SqliteNoteStore store(db);

  domain::Note n;
  n.id = domain::NoteId{std::string{"note-upd"}};
  n.folder_id = domain::FolderId{std::string{"root"}};
  n.title = "Before";
  n.content = domain::NoteContent::from_plain_text("before");
  n.created_at_ms = 1;
  n.modified_at_ms = 1;
  n.revision = 0;
  auto s1 = store.save(n);
  require(s1.has_value() && s1.value().revision == 1, "insert");

  require(static_cast<bool>(db->exec("ALTER TABLE notes_search RENAME TO notes_search_broken;")),
          "break search");

  domain::Note n2 = s1.value();
  n2.title = "After";
  n2.content = domain::NoteContent::from_plain_text("after");
  n2.revision = 1;
  n2.modified_at_ms = 2;
  auto s2 = store.save(n2);
  require(!s2.has_value(), "update fail closed");

  // Restore table name is not needed; verify note body unchanged via raw reopen
  // after fixing table — recreate search table and check old content remains.
  require(static_cast<bool>(db->exec(
              "ALTER TABLE notes_search_broken RENAME TO notes_search;")),
          "restore");
  auto loaded = store.load(n.id);
  require(loaded.has_value(), "still present");
  require(loaded.value().title == "Before", "title not partially updated");
  require(loaded.value().revision == 1, "rev unchanged");
  require(loaded.value().content.plain_text() == "before", "body unchanged");
}

}  // namespace

int main() {
  try {
    test_search_row_present_after_save();
    test_save_fails_closed_when_search_table_missing();
    test_update_fails_closed_when_search_broken();
    std::cerr << "search_index_failclosed_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "search_index_failclosed_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
