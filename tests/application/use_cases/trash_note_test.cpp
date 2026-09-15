#include "application/use_cases/notes/delete_note.hpp"
#include "application/use_cases/notes/list_trashed_notes.hpp"
#include "application/use_cases/notes/move_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/restore_note.hpp"
#include "application/use_cases/notes/trash_note.hpp"
#include "domain/notes/note_content.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}

void seed_root_and_note(notes::testing::InMemoryNoteStore& store,
                        notes::domain::Note& out) {
  notes::domain::Folder root;
  root.id = notes::domain::FolderId{"root"};
  root.name = "Notes";
  require(static_cast<bool>(store.save(root)), "root");
  notes::domain::Folder work;
  work.id = notes::domain::FolderId{"work"};
  work.name = "Work";
  require(static_cast<bool>(store.save(work)), "work");

  out.id = notes::domain::NoteId{"note-uc-1"};
  out.folder_id = work.id;
  out.title = "Hello";
  out.content = notes::domain::NoteContent::from_plain_text("world");
  out.created_at_ms = 1;
  out.modified_at_ms = 1;
  out.revision = 0;
  require(static_cast<bool>(store.save(out)), "save note");
  out = store.load(out.id).value();
}

}  // namespace

int main() {
  try {
    using namespace notes;
    testing::InMemoryNoteStore store;
    testing::FixedClock clock{55'000};
    domain::Note note;
    seed_root_and_note(store, note);

    application::DeleteNote del{store, clock};
    require(static_cast<bool>(del.execute(note.id)), "delete soft");
    require(store.list(domain::FolderId{"work"}).value().empty(), "not listed");
    require(store.list_trashed().value().size() == 1, "trashed listed");

    application::RestoreNote restore{store, store, store};
    auto r = restore.execute(note.id);
    require(r && r.value().folder_id.value() == "work", "restored work");

    application::MoveNote move{store, store, store, clock};
    auto m = move.execute({note.id, domain::FolderId{"root"}});
    require(m && m.value().folder_id.value() == "root", "moved");

    application::TrashNote trash{store, clock};
    require(static_cast<bool>(trash.execute(note.id)), "trash");
    application::ListTrashedNotes list_trash{store};
    require(list_trash.execute().value().size() == 1, "list trash uc");

    application::PurgeNote purge{store, store, nullptr};
    require(static_cast<bool>(purge.execute(note.id)), "purge");
    require(!store.load(note.id), "gone");

    // Cannot purge active note.
    domain::Note n2;
    seed_root_and_note(store, n2);
    auto bad = purge.execute(n2.id);
    require(!bad && bad.error().kind == application::ErrorKind::ValidationFailed,
            "purge active fails");

    std::cerr << "trash_note_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "trash_note_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
