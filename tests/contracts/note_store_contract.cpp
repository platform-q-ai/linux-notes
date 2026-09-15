#include <catch_amalgamated.hpp>

#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_folder_store.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <filesystem>
#include <memory>

namespace {

template <typename NoteStore>
void run_note_ops(NoteStore& store, const notes::domain::FolderId& folder_id) {
  notes::domain::Note note;
  note.id = notes::domain::NoteId{"n1"};
  note.folder_id = folder_id;
  note.title = "Hello";
  note.content = notes::domain::NoteContent::from_plain_text("World");
  note.created_at_ms = 1;
  note.modified_at_ms = 1;
  note.revision = 0;
  auto saved = store.save(note);
  REQUIRE(saved);
  REQUIRE(saved.value().revision == 1);

  auto loaded = store.load(note.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().title == "Hello");
  REQUIRE(loaded.value().content.plain_text() == "World");

  auto listed = store.list(folder_id);
  REQUIRE(listed);
  REQUIRE(listed.value().size() == 1);

  note = loaded.value();
  note.title = "Hello2";
  auto saved2 = store.save(note);
  REQUIRE(saved2);
  REQUIRE(saved2.value().revision == 2);

  note.revision = 1;  // stale base
  note.title = "stale";
  auto conflict = store.save(note);
  REQUIRE_FALSE(conflict);
  REQUIRE(conflict.error().kind ==
          notes::application::ErrorKind::RevisionConflict);

  auto found = store.search("Hello");
  REQUIRE(found);
  REQUIRE_FALSE(found.value().empty());

  REQUIRE(store.trash(notes::domain::NoteId{"n1"}, 5000));
  auto trashed = store.load(notes::domain::NoteId{"n1"});
  REQUIRE(trashed);
  REQUIRE(trashed.value().is_trashed());
  auto listed_after = store.list(folder_id);
  REQUIRE(listed_after);
  REQUIRE(listed_after.value().empty());
  auto trash_list = store.list_trashed();
  REQUIRE(trash_list);
  REQUIRE(trash_list.value().size() == 1);
  auto restored = store.restore(notes::domain::NoteId{"n1"}, folder_id);
  REQUIRE(restored);
  REQUIRE_FALSE(restored.value().is_trashed());

  REQUIRE(store.trash(notes::domain::NoteId{"n1"}, 6000));
  REQUIRE(store.remove(notes::domain::NoteId{"n1"}));
  REQUIRE_FALSE(store.load(notes::domain::NoteId{"n1"}));
}

}  // namespace

TEST_CASE("in-memory note store contract") {
  notes::testing::InMemoryNoteStore store;
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"folder-1"};
  folder.name = "Notes";
  folder.created_at_ms = 1;
  folder.modified_at_ms = 1;
  REQUIRE(store.save(folder));
  run_note_ops(store, folder.id);
}

TEST_CASE("sqlite note store contract") {
  const auto path =
      std::filesystem::temp_directory_path() / "linux-notes-catch-contract.db";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  auto db = std::make_shared<notes::adapters::persistence::SqliteDb>();
  REQUIRE(db->open(path));

  notes::adapters::persistence::SqliteFolderStore folders(db);
  notes::adapters::persistence::SqliteNoteStore notes(db);

  // migrate seeds root; also add a child folder
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"folder-1"};
  folder.name = "Notes";
  folder.parent_id = notes::domain::FolderId{"root"};
  folder.created_at_ms = 1;
  folder.modified_at_ms = 1;
  REQUIRE(folders.save(folder));

  run_note_ops(notes, folder.id);
  std::filesystem::remove(path, ec);
}
