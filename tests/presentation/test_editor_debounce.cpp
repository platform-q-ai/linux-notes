#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <QCoreApplication>
#include <QTest>

namespace {

void pump(int rounds = 25) {
  for (int i = 0; i < rounds; ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(15);
  }
}

class FailingNoteWriter final : public notes::application::NoteWriter {
public:
  [[nodiscard]] notes::application::Result<notes::domain::Note> save(
      const notes::domain::Note&) override {
    return notes::application::Result<notes::domain::Note>::fail(
        {notes::application::ErrorKind::StorageFailure, "injected failure"});
  }
  [[nodiscard]] notes::application::Result<void> remove(
      const notes::domain::NoteId&) override {
    return notes::application::Result<void>::ok();
  }
};

}  // namespace

TEST_CASE("editor openNote generation ignores stale completion",
          "[presentation][stale][headless]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{5000};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::application::CreateNote create{store, clock};
  notes::application::LoadNote load{store};
  notes::application::SaveNote save{store, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

  auto a = create.execute({folder.id, "A",
                           notes::domain::NoteContent::from_plain_text("aaa")});
  auto b = create.execute({folder.id, "B",
                           notes::domain::NoteContent::from_plain_text("bbb")});
  REQUIRE(a);
  REQUIRE(b);

  editor.openNote(QString::fromStdString(a.value().id.value()));
  editor.openNote(QString::fromStdString(b.value().id.value()));
  pump();

  REQUIRE(editor.noteId().toStdString() == b.value().id.value());
  REQUIRE(editor.plainText().contains(QStringLiteral("bbb")));
  dispatcher.shutdown();
}

TEST_CASE("failed save keeps dirty and error state",
          "[presentation][error][dirty][headless]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{6000};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::application::CreateNote create{store, clock};
  notes::application::LoadNote load{store};
  FailingNoteWriter fail_writer;
  notes::application::SaveNote save{fail_writer, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

  // Seed via store directly so LoadNote works; SaveNote uses failing writer.
  notes::domain::Note seed;
  seed.id = notes::domain::NoteId{"note-err-1"};
  seed.folder_id = folder.id;
  seed.title = "t";
  seed.content = notes::domain::NoteContent::from_plain_text("x");
  seed.revision = 0;
  seed.created_at_ms = 1;
  seed.modified_at_ms = 1;
  REQUIRE(store.save(seed));

  editor.openNote(QString::fromStdString(seed.id.value()));
  pump();
  REQUIRE_FALSE(editor.noteId().isEmpty());

  editor.setHtml(QStringLiteral("<p>local-edit</p>"));
  REQUIRE(editor.dirty());
  editor.saveNow();
  pump(40);

  REQUIRE(editor.dirty());
  REQUIRE(editor.saveState() == QStringLiteral("error"));
  REQUIRE_FALSE(editor.errorString().isEmpty());
  dispatcher.shutdown();
}

TEST_CASE("note switch while dirty schedules save without crash",
          "[presentation][debounce][headless]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{7000};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::application::CreateNote create{store, clock};
  notes::application::LoadNote load{store};
  notes::application::SaveNote save{store, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

  auto a = create.execute({folder.id, "A",
                           notes::domain::NoteContent::from_plain_text("one")});
  auto b = create.execute({folder.id, "B",
                           notes::domain::NoteContent::from_plain_text("two")});
  REQUIRE(a);
  REQUIRE(b);

  editor.openNote(QString::fromStdString(a.value().id.value()));
  pump();
  editor.setHtml(QStringLiteral("<p>dirty-a</p>"));
  REQUIRE(editor.dirty());
  // Switch during dirty/debounce window
  editor.openNote(QString::fromStdString(b.value().id.value()));
  pump(40);
  REQUIRE(editor.noteId().toStdString() == b.value().id.value());
  auto reloaded_a = store.load(a.value().id);
  REQUIRE(reloaded_a);
  // Prior note should not hold B's body; A either saved dirty-a or kept one.
  REQUIRE_FALSE(reloaded_a.value().content.plain_text().find("two") !=
                std::string::npos);
  dispatcher.shutdown();
}
