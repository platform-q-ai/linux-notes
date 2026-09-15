#include <catch_amalgamated.hpp>
#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

TEST_CASE("create and save note bumps revision") {
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{1000};
  notes::application::CreateNote create{store, clock};
  notes::application::SaveNote save{store, store, clock};

  auto folder = notes::domain::Folder{};
  folder.id = notes::domain::FolderId{"f1"};
  folder.name = "Notes";
  REQUIRE(store.save(folder));

  notes::application::CreateNote::Request creq;
  creq.folder_id = folder.id;
  creq.title = "t";
  creq.content = notes::domain::NoteContent::from_plain_text("body");
  auto created = create.execute(creq);
  REQUIRE(created);
  REQUIRE(created.value().revision == 1);

  auto note = created.value();
  note.content = notes::domain::NoteContent::from_plain_text("body2");
  auto saved = save.execute({note, true});
  REQUIRE(saved);
  REQUIRE(saved.value().saved.revision == 2);
  REQUIRE_FALSE(saved.value().kept_both);
}

TEST_CASE("revision conflict keep-both") {
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{2000};
  notes::application::CreateNote create{store, clock};
  notes::application::SaveNote save{store, store, clock};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f1"};
  folder.name = "Notes";
  REQUIRE(store.save(folder));

  notes::application::CreateNote::Request creq;
  creq.folder_id = folder.id;
  creq.title = "t";
  creq.content = notes::domain::NoteContent::from_plain_text("a");
  auto created = create.execute(creq);
  REQUIRE(created);

  auto stale = created.value();
  auto fresh = created.value();
  fresh.content = notes::domain::NoteContent::from_plain_text("newer");
  REQUIRE(save.execute({fresh, true}));

  stale.content = notes::domain::NoteContent::from_plain_text("stale edit");
  auto conflict = save.execute({stale, true});
  REQUIRE(conflict);
  REQUIRE(conflict.value().kept_both);
  REQUIRE(conflict.value().saved.id != created.value().id);
}
