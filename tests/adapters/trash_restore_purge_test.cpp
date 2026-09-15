#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_folder_store.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "application/use_cases/notes/move_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/restore_note.hpp"
#include "application/use_cases/notes/trash_note.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_content.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_attachment_store.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}

std::filesystem::path temp_db(const char* name) {
  const auto dir =
      std::filesystem::temp_directory_path() / "linux-notes-trash";
  std::filesystem::create_directories(dir);
  auto path = dir / name;
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(std::filesystem::path(path.string() + "-wal"), ec);
  std::filesystem::remove(std::filesystem::path(path.string() + "-shm"), ec);
  return path;
}

void test_sqlite_trash_restore_purge_and_folder() {
  using namespace notes;
  const auto path = temp_db("trash.db");
  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(path)), "open");
  adapters::persistence::SqliteNoteStore notes(db);
  adapters::persistence::SqliteFolderStore folders(db);
  testing::FixedClock clock{10'000};

  domain::Folder work;
  work.id = domain::FolderId{"folder-work"};
  work.name = "Work";
  work.parent_id = domain::FolderId{"root"};
  work.created_at_ms = 1;
  work.modified_at_ms = 1;
  require(static_cast<bool>(folders.save(work)), "save folder");

  domain::Note n;
  n.id = domain::NoteId{"note-1"};
  n.folder_id = work.id;
  n.title = "Important";
  n.content = domain::NoteContent::from_plain_text("secret body");
  n.created_at_ms = 1;
  n.modified_at_ms = 1;
  n.revision = 0;
  n.pinned = true;
  require(static_cast<bool>(notes.save(n)), "save note");

  application::TrashNote trash_uc{notes, clock};
  require(static_cast<bool>(trash_uc.execute(n.id)), "trash");

  auto listed = notes.list(work.id);
  require(listed && listed.value().empty(), "hidden from folder list");
  auto search = notes.search("secret");
  require(search && search.value().empty(), "hidden from search");
  auto trash_list = notes.list_trashed();
  require(trash_list && trash_list.value().size() == 1, "in trash");
  require(trash_list.value()[0].pinned, "pin preserved in summary");

  auto loaded = notes.load(n.id);
  require(loaded && loaded.value().is_trashed(), "load trashed");
  require(loaded.value().trashed_from_folder_id &&
              *loaded.value().trashed_from_folder_id == work.id,
          "from folder remembered");
  require(loaded.value().folder_id.value() == "root", "parked under root");
  require(loaded.value().content.plain_text().find("secret") != std::string::npos,
          "body kept");

  // Folder with only previously-active notes now empty → delete ok.
  require(static_cast<bool>(folders.remove(work.id)), "delete empty work folder");

  application::RestoreNote restore_uc{notes, notes, folders};
  auto restored = restore_uc.execute(n.id);
  require(static_cast<bool>(restored),
          restored ? "restored" : restored.error().message.c_str());
  // Prior folder gone → fallback root.
  require(restored.value().folder_id.value() == "root", "fallback root");
  require(!restored.value().is_trashed(), "active again");
  auto search2 = notes.search("secret");
  require(static_cast<bool>(search2) && search2.value().size() == 1,
          "searchable after restore");

  // Re-trash and purge permanently.
  require(static_cast<bool>(trash_uc.execute(n.id)), "trash again");
  application::PurgeNote purge_uc{notes, notes, nullptr};
  require(static_cast<bool>(purge_uc.execute(n.id)), "purge");
  auto gone = notes.load(n.id);
  require(!gone && gone.error().kind == application::ErrorKind::NotFound, "purged");
}

void test_purge_requires_trash_and_gc_attachments() {
  using namespace notes;
  testing::InMemoryNoteStore store;
  testing::FixedClock clock{20'000};
  testing::InMemoryAttachmentStore attachments{clock};

  domain::Folder root;
  root.id = domain::FolderId{"root"};
  root.name = "Notes";
  require(static_cast<bool>(store.save(root)), "root");

  domain::Note n;
  n.id = domain::NoteId{"n-att"};
  n.folder_id = root.id;
  n.title = "with att";
  n.revision = 0;
  n.created_at_ms = 1;
  n.modified_at_ms = 1;

  auto put = attachments.put(n.id, "a.bin", "application/octet-stream",
                             std::vector<std::uint8_t>{1, 2, 3});
  require(static_cast<bool>(put), "put att");
  domain::AttachmentRefBlock ref;
  ref.attachment_id = put.value().id;
  ref.display_name = "a.bin";
  n.content = domain::NoteContent{std::vector<domain::ContentBlock>{ref}};
  require(static_cast<bool>(store.save(n)), "save");

  application::PurgeNote purge{store, store, &attachments};
  auto early = purge.execute(n.id);
  require(!early && early.error().kind == application::ErrorKind::ValidationFailed,
          "purge without trash fails");

  application::TrashNote trash{store, clock};
  require(static_cast<bool>(trash.execute(n.id)), "trash");
  require(static_cast<bool>(purge.execute(n.id)), "purge ok");
  require(!static_cast<bool>(store.load(n.id)), "note gone");
  auto blob = attachments.get(put.value().id);
  require(!blob && blob.error().kind == application::ErrorKind::NotFound,
          "attachment GC");
}

void test_move_note_and_pin_order() {
  using namespace notes;
  testing::InMemoryNoteStore store;
  testing::FixedClock clock{30'000};

  domain::Folder a;
  a.id = domain::FolderId{"a"};
  a.name = "A";
  domain::Folder b;
  b.id = domain::FolderId{"b"};
  b.name = "B";
  require(static_cast<bool>(store.save(a)) && static_cast<bool>(store.save(b)),
          "folders");

  domain::Note n;
  n.id = domain::NoteId{"m1"};
  n.folder_id = a.id;
  n.title = "pinned";
  n.pinned = true;
  n.revision = 0;
  n.created_at_ms = 1;
  n.modified_at_ms = 100;
  n.content = domain::NoteContent::from_plain_text("x");
  require(static_cast<bool>(store.save(n)), "save");

  domain::Note n2;
  n2.id = domain::NoteId{"m2"};
  n2.folder_id = b.id;
  n2.title = "other";
  n2.pinned = false;
  n2.revision = 0;
  n2.created_at_ms = 1;
  n2.modified_at_ms = 200;
  n2.content = domain::NoteContent::from_plain_text("y");
  require(static_cast<bool>(store.save(n2)), "save2");

  application::MoveNote move{store, store, store, clock};
  auto moved = move.execute({n.id, b.id});
  require(static_cast<bool>(moved) && moved.value().folder_id == b.id, "moved");

  auto list_b = store.list(b.id);
  require(static_cast<bool>(list_b) && list_b.value().size() == 2, "both in b");
  require(list_b.value().front().id == n.id, "pinned first after move");
}

void test_restore_to_prior_folder() {
  using namespace notes;
  testing::InMemoryNoteStore store;
  testing::FixedClock clock{40'000};
  domain::Folder root;
  root.id = domain::FolderId{"root"};
  root.name = "Notes";
  domain::Folder work;
  work.id = domain::FolderId{"work"};
  work.name = "Work";
  require(static_cast<bool>(store.save(root)) && static_cast<bool>(store.save(work)),
          "folders");

  domain::Note n;
  n.id = domain::NoteId{"r1"};
  n.folder_id = work.id;
  n.title = "t";
  n.revision = 0;
  n.created_at_ms = 1;
  n.modified_at_ms = 1;
  n.content = domain::NoteContent::from_plain_text("body");
  require(static_cast<bool>(store.save(n)), "save");

  application::TrashNote trash{store, clock};
  require(static_cast<bool>(trash.execute(n.id)), "trash");
  application::RestoreNote restore{store, store, store};
  auto r = restore.execute(n.id);
  require(static_cast<bool>(r) && r.value().folder_id == work.id, "back to work");
  require(!r.value().is_trashed(), "active");
}

}  // namespace

int main() {
  try {
    test_sqlite_trash_restore_purge_and_folder();
    test_purge_requires_trash_and_gc_attachments();
    test_move_note_and_pin_order();
    test_restore_to_prior_folder();
    std::cerr << "trash_restore_purge_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "trash_restore_purge_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
