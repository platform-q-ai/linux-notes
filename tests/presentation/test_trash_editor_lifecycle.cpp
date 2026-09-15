#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/use_cases/notes/trash_note.hpp"
#include "domain/notes/note_content.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <QCoreApplication>
#include <QTest>

namespace {

void pump(int rounds = 40) {
  for (int i = 0; i < rounds; ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(10);
  }
}

}  // namespace

TEST_CASE("open trashed note round-trips trash metadata; edits refused",
          "[editor][trash][p0]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{42'000};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::domain::Note seed;
  seed.id = notes::domain::NoteId{"note-trash-1"};
  seed.folder_id = folder.id;
  seed.title = "body";
  seed.content = notes::domain::NoteContent::from_plain_text("alive");
  seed.revision = 0;
  seed.created_at_ms = 1;
  seed.modified_at_ms = 1;
  REQUIRE(store.save(seed));

  notes::application::LoadNote load{store};
  notes::application::SaveNote save{store, store, clock};
  notes::application::TrashNote trash{store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

  REQUIRE(trash.execute(seed.id));
  auto after_trash = store.load(seed.id);
  REQUIRE(after_trash);
  REQUIRE(after_trash.value().is_trashed());
  const auto trashed_at = after_trash.value().trashed_at_ms;

  editor.openNote(QString::fromStdString(seed.id.value()));
  pump();
  REQUIRE(editor.noteId() == QString::fromStdString(seed.id.value()));
  REQUIRE(editor.noteTrashed());

  // setHtml while trashed must not dirty or change durable body.
  editor.setHtml(QStringLiteral("<p>resurrect attempt</p>"));
  REQUIRE_FALSE(editor.dirty());
  REQUIRE(editor.flushPendingSavesBlocking());

  auto loaded = store.load(seed.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().is_trashed());
  REQUIRE(loaded.value().trashed_at_ms == trashed_at);
  REQUIRE(loaded.value().content.plain_text().find("alive") != std::string::npos);
  REQUIRE(loaded.value().content.plain_text().find("resurrect") ==
          std::string::npos);
}

TEST_CASE("discardEditorWithoutFlush then purge does not resurrect",
          "[editor][trash][purge][p0]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{43'000};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::domain::Note seed;
  seed.id = notes::domain::NoteId{"note-purge-1"};
  seed.folder_id = folder.id;
  seed.title = "p";
  seed.content = notes::domain::NoteContent::from_plain_text("x");
  seed.revision = 0;
  seed.created_at_ms = 1;
  seed.modified_at_ms = 1;
  REQUIRE(store.save(seed));

  notes::application::LoadNote load{store};
  notes::application::SaveNote save{store, store, clock};
  notes::application::TrashNote trash{store, clock};
  notes::application::PurgeNote purge{store, store, nullptr};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

  REQUIRE(trash.execute(seed.id));
  editor.openNote(QString::fromStdString(seed.id.value()));
  pump();
  REQUIRE(editor.noteTrashed());

  // Even if UI tried to mark dirty, discard path clears without flush-resurrect.
  editor.discardEditorWithoutFlush();
  REQUIRE(editor.noteId().isEmpty());
  REQUIRE(purge.execute(seed.id));
  REQUIRE_FALSE(store.load(seed.id));
}
