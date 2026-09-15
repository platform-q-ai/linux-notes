#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/use_cases/notes/trash_note.hpp"
#include "presentation/qt/app_exit_gate.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"
#include "tests/support/fakes/controllable_note_writer.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"
#include "tests/support/structured_fixtures.hpp"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <string>
#include <vector>

namespace {

void pump(int rounds = 30) {
  for (int i = 0; i < rounds; ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(10);
  }
}

struct Fixture {
  int argc = 0;
  QCoreApplication app{argc, nullptr};
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{42'000};
  notes::testing::ControllableNoteWriter writer{store};
  notes::application::LoadNote load{store};
  notes::application::SaveNote save{writer, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher,
                                              nullptr, nullptr, nullptr};
  notes::presentation::AppExitGate gate{&editor, &dispatcher};
  std::vector<notes::domain::NoteId> ids;

  Fixture() {
    notes::domain::Folder folder;
    folder.id = notes::domain::FolderId{"f"};
    folder.name = "N";
    REQUIRE(store.save(folder));

    for (int i = 0; i < 4; ++i) {
      notes::domain::Note n;
      n.id = notes::domain::NoteId{"rapid-" + std::to_string(i)};
      n.folder_id = folder.id;
      n.title = "T" + std::to_string(i);
      n.content = notes::domain::NoteContent::from_plain_text(
          "body-" + std::to_string(i));
      n.revision = 0;
      n.created_at_ms = i;
      n.modified_at_ms = i;
      auto s = store.save(n);
      REQUIRE(s);
      ids.push_back(s.value().id);
    }
  }

  ~Fixture() { dispatcher.shutdown(); }
};

}  // namespace

TEST_CASE("rapid switch between notes keeps final selection coherent",
          "[presentation][rapid][switch][headless]") {
  Fixture fx;
  for (const auto& id : fx.ids) {
    fx.editor.openNote(QString::fromStdString(id.value()));
  }
  pump();
  REQUIRE(fx.editor.noteId().toStdString() == fx.ids.back().value());
  REQUIRE_FALSE(fx.editor.loading());
  REQUIRE(fx.editor.plainText().contains(QStringLiteral("body-3")));
}

TEST_CASE("rapid switch discards in-flight dirty of abandoned note without crash",
          "[presentation][rapid][switch][stale][headless]") {
  Fixture fx;
  fx.editor.openNote(QString::fromStdString(fx.ids[0].value()));
  pump();
  fx.editor.setHtml(QStringLiteral("<p>dirty-A</p>"));
  REQUIRE(fx.editor.dirty());

  // Immediately open another note (stale save may still be queued).
  fx.editor.openNote(QString::fromStdString(fx.ids[1].value()));
  pump(40);
  REQUIRE(fx.editor.noteId().toStdString() == fx.ids[1].value());
  // Either flushed A or abandoned without corrupting B.
  auto b = fx.store.load(fx.ids[1]);
  REQUIRE(b);
  REQUIRE(b.value().content.plain_text().find("body-1") != std::string::npos);
}

TEST_CASE("trash while editor open then switch clears selection safely",
          "[presentation][rapid][trash][headless]") {
  Fixture fx;
  fx.editor.openNote(QString::fromStdString(fx.ids[0].value()));
  pump();
  notes::application::TrashNote trash{fx.writer, fx.clock};
  REQUIRE(trash.execute(fx.ids[0]));
  // Switch to another note after underlying delete/trash.
  fx.editor.openNote(QString::fromStdString(fx.ids[2].value()));
  pump();
  REQUIRE(fx.editor.noteId().toStdString() == fx.ids[2].value());
  auto gone = fx.store.load(fx.ids[0]);
  REQUIRE(gone);
  REQUIRE(gone.value().is_trashed());
}

TEST_CASE("quit during rapid dirty switch: fail then preserve or flush",
          "[presentation][rapid][quit][headless]") {
  Fixture fx;
  fx.editor.openNote(QString::fromStdString(fx.ids[0].value()));
  pump();
  fx.editor.setHtml(QStringLiteral("<p>quit-me</p>"));
  REQUIRE(fx.editor.dirty());

  fx.writer.fail_next_save = true;
  REQUIRE_FALSE(fx.gate.requestClose());
  REQUIRE(fx.gate.closeBlocked());
  REQUIRE(fx.editor.dirty());

  fx.writer.fail_next_save = false;
  REQUIRE(fx.gate.retryClose());
  REQUIRE(fx.gate.quitAuthorized());
  REQUIRE_FALSE(fx.editor.dirty());
}

TEST_CASE("structured note rapid reopen preserves checklist after save",
          "[presentation][rapid][structured][headless]") {
  Fixture fx;
  auto structured = notes::testing::make_structured_note("rapid-struct", "f");
  auto saved = fx.store.save(structured);
  REQUIRE(saved);

  fx.editor.openNote(QString::fromStdString(saved.value().id.value()));
  pump();
  fx.editor.openNote(QString::fromStdString(fx.ids[0].value()));
  pump();
  fx.editor.openNote(QString::fromStdString(saved.value().id.value()));
  pump();
  REQUIRE(fx.editor.noteId().toStdString() == saved.value().id.value());
  auto reloaded = fx.store.load(saved.value().id);
  REQUIRE(reloaded);
  REQUIRE(notes::testing::content_has_checklist_and_attachment(
      reloaded.value().content));
}
