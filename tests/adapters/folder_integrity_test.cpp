#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_folder_store.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "domain/folders/folder.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_content.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}

void test_sqlite_reject_delete_with_notes() {
  using namespace notes;
  const auto dir =
      std::filesystem::temp_directory_path() / "linux-notes-folder-integrity";
  std::filesystem::create_directories(dir);
  const auto db_path = dir / "folder.db";
  std::error_code ec;
  std::filesystem::remove(db_path, ec);

  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(db_path)), "open db");

  adapters::persistence::SqliteFolderStore folders(db);
  adapters::persistence::SqliteNoteStore notes_store(db);

  domain::Folder f;
  f.id = domain::FolderId{std::string{"folder-work"}};
  f.name = "Work";
  f.parent_id = domain::FolderId{std::string{"root"}};
  f.sort_order = 1;
  f.created_at_ms = 1;
  f.modified_at_ms = 1;
  require(static_cast<bool>(folders.save(f)), "save folder");

  domain::Note n;
  n.id = domain::NoteId{std::string{"note-in-work"}};
  n.folder_id = f.id;
  n.title = "t";
  n.content = domain::NoteContent::from_plain_text("body");
  n.created_at_ms = 1;
  n.modified_at_ms = 1;
  n.revision = 0;
  require(static_cast<bool>(notes_store.save(n)), "save note");

  auto del = folders.remove(f.id);
  require(!del.has_value(), "delete must fail while notes remain");
  require(del.error().kind == application::ErrorKind::ValidationFailed,
          "validation kind");

  // Note still reachable and listable under folder.
  auto loaded = notes_store.load(n.id);
  require(loaded.has_value(), "note not orphaned/deleted");
  require(loaded.value().folder_id == f.id, "folder_id intact");
  auto listed = notes_store.list(f.id);
  require(listed.has_value() && listed.value().size() == 1, "still listed");

  // After moving note out, delete succeeds.
  domain::Note moved = loaded.value();
  moved.folder_id = domain::FolderId{std::string{"root"}};
  moved.revision = loaded.value().revision;
  require(static_cast<bool>(notes_store.save(moved)), "move note");
  require(static_cast<bool>(folders.remove(f.id)), "delete empty folder");
  auto gone = folders.load(f.id);
  require(!gone.has_value() &&
              gone.error().kind == application::ErrorKind::NotFound,
          "folder gone");
}

void test_sqlite_reject_delete_with_child_folder() {
  using namespace notes;
  const auto dir =
      std::filesystem::temp_directory_path() / "linux-notes-folder-child";
  std::filesystem::create_directories(dir);
  const auto db_path = dir / "child.db";
  std::error_code ec;
  std::filesystem::remove(db_path, ec);

  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(db_path)), "open");

  adapters::persistence::SqliteFolderStore folders(db);

  domain::Folder parent;
  parent.id = domain::FolderId{std::string{"folder-parent"}};
  parent.name = "Parent";
  parent.parent_id = domain::FolderId{std::string{"root"}};
  parent.created_at_ms = 1;
  parent.modified_at_ms = 1;
  require(static_cast<bool>(folders.save(parent)), "parent");

  domain::Folder child;
  child.id = domain::FolderId{std::string{"folder-child"}};
  child.name = "Child";
  child.parent_id = parent.id;
  child.created_at_ms = 1;
  child.modified_at_ms = 1;
  require(static_cast<bool>(folders.save(child)), "child");

  auto del = folders.remove(parent.id);
  require(!del.has_value() &&
              del.error().kind == application::ErrorKind::ValidationFailed,
          "parent with child rejected");
}

void test_memory_reject_delete_with_notes() {
  using namespace notes;
  testing::InMemoryNoteStore store;
  domain::Folder f;
  f.id = domain::FolderId{std::string{"f1"}};
  f.name = "F";
  require(static_cast<bool>(store.save(f)), "folder");
  domain::Note n;
  n.id = domain::NoteId{std::string{"n1"}};
  n.folder_id = f.id;
  n.title = "t";
  n.content = domain::NoteContent::from_plain_text("x");
  n.revision = 0;
  require(static_cast<bool>(store.save(n)), "note");
  auto del = store.remove(f.id);
  require(!del.has_value() &&
              del.error().kind == application::ErrorKind::ValidationFailed,
          "mem reject");
}

void test_schema_has_fk() {
  using namespace notes;
  const auto dir =
      std::filesystem::temp_directory_path() / "linux-notes-schema-fk";
  std::filesystem::create_directories(dir);
  const auto db_path = dir / "fk.db";
  std::error_code ec;
  std::filesystem::remove(db_path, ec);

  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(db_path)), "open");

  // Direct insert with unknown folder_id must fail under FK.
  auto bad = db->exec(
      "INSERT INTO notes(id,folder_id,title,body,created_at,modified_at,"
      "revision,pinned) VALUES('x','no-such-folder','','',0,0,1,0);");
  require(!bad.has_value(), "FK rejects dangling folder_id");
}

}  // namespace

int main() {
  try {
    test_sqlite_reject_delete_with_notes();
    test_sqlite_reject_delete_with_child_folder();
    test_memory_reject_delete_with_notes();
    test_schema_has_fk();
    std::cerr << "folder_integrity_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "folder_integrity_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
