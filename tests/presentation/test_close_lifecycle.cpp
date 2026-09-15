#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

#include "application/ports/notes/note_writer.hpp"
#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "presentation/qt/app_exit_gate.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <atomic>

namespace {

void pump(int rounds = 25) {
  for (int i = 0; i < rounds; ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(15);
  }
}

class ControllableNoteWriter final : public notes::application::NoteWriter {
public:
  explicit ControllableNoteWriter(notes::application::NoteWriter& inner)
      : inner_(inner) {}

  std::atomic<bool> fail_next{false};
  std::atomic<int> save_calls{0};

  [[nodiscard]] notes::application::Result<notes::domain::Note> save(
      const notes::domain::Note& note) override {
    ++save_calls;
    if (fail_next.load()) {
      return notes::application::Result<notes::domain::Note>::fail(
          {notes::application::ErrorKind::StorageFailure,
           "injected close-lifecycle failure"});
    }
    return inner_.save(note);
  }

  [[nodiscard]] notes::application::Result<void> trash(
      const notes::domain::NoteId& id, std::int64_t at) override {
    return inner_.trash(id, at);
  }

  [[nodiscard]] notes::application::Result<notes::domain::Note> restore(
      const notes::domain::NoteId& id,
      const notes::domain::FolderId& folder) override {
    return inner_.restore(id, folder);
  }

  [[nodiscard]] notes::application::Result<void> remove(
      const notes::domain::NoteId& id) override {
    return inner_.remove(id);
  }

private:
  notes::application::NoteWriter& inner_;
};

struct Fixture {
  int argc = 0;
  QCoreApplication app{argc, nullptr};
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{9100};
  ControllableNoteWriter writer{store};
  notes::application::LoadNote load{store};
  notes::application::SaveNote save{writer, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};
  notes::presentation::AppExitGate gate{&editor, &dispatcher};
  notes::domain::Note seed;

  Fixture() {
    notes::domain::Folder folder;
    folder.id = notes::domain::FolderId{"f"};
    folder.name = "N";
    REQUIRE(store.save(folder));

    seed.id = notes::domain::NoteId{"note-close-1"};
    seed.folder_id = folder.id;
    seed.title = "t";
    seed.content = notes::domain::NoteContent::from_plain_text("seed");
    seed.revision = 0;
    seed.created_at_ms = 1;
    seed.modified_at_ms = 1;
    REQUIRE(store.save(seed));
  }

  ~Fixture() { dispatcher.shutdown(); }

  void open_and_dirty(const QString& html) {
    editor.openNote(QString::fromStdString(seed.id.value()));
    pump();
    REQUIRE_FALSE(editor.noteId().isEmpty());
    editor.setHtml(html);
    REQUIRE(editor.dirty());
  }
};

}  // namespace

TEST_CASE("close lifecycle: failed flush vetoes quit and preserves dirty doc",
          "[presentation][close][lifecycle][headless][quit]") {
  Fixture fx;
  fx.writer.fail_next = true;
  fx.open_and_dirty(QStringLiteral("<p>must-not-discard-on-close</p>"));

  QSignalSpy failed_spy(&fx.gate,
                        &notes::presentation::AppExitGate::closeFailed);
  QSignalSpy ok_spy(&fx.gate,
                    &notes::presentation::AppExitGate::closeSucceeded);

  // Simulates ApplicationWindow.onClosing → exitGate.requestClose()
  REQUIRE_FALSE(fx.gate.requestClose());
  REQUIRE(fx.gate.closeBlocked());
  REQUIRE_FALSE(fx.gate.quitAuthorized());
  REQUIRE(failed_spy.count() >= 1);
  REQUIRE(ok_spy.count() == 0);
  REQUIRE(fx.gate.blockReason().contains(QStringLiteral("Could not save")));

  // Dirty editable document preserved — never silent discard.
  REQUIRE(fx.editor.dirty());
  REQUIRE(fx.editor.noteId().toStdString() == fx.seed.id.value());
  REQUIRE(fx.editor.plainText().contains(QStringLiteral("must-not-discard")));
  REQUIRE_FALSE(fx.editor.errorString().isEmpty());

  // aboutToQuit-style best effort still fails and does not authorize quit.
  REQUIRE_FALSE(fx.gate.flushBestEffort());
  REQUIRE_FALSE(fx.gate.quitAuthorized());
  REQUIRE(fx.editor.dirty());
}

TEST_CASE("close lifecycle: retry after failure then success authorizes quit",
          "[presentation][close][lifecycle][headless][quit][retry]") {
  Fixture fx;
  fx.writer.fail_next = true;
  fx.open_and_dirty(QStringLiteral("<p>retry-then-quit</p>"));

  REQUIRE_FALSE(fx.gate.requestClose());
  REQUIRE(fx.editor.dirty());
  REQUIRE_FALSE(fx.gate.quitAuthorized());

  // User keeps editing: banner dismiss without clearing dirty.
  fx.gate.acknowledgeBlock();
  REQUIRE_FALSE(fx.gate.closeBlocked());
  REQUIRE(fx.editor.dirty());

  // Retry after storage recovers (user-visible Retry save & quit).
  fx.writer.fail_next = false;
  QSignalSpy ok_spy(&fx.gate,
                    &notes::presentation::AppExitGate::closeSucceeded);
  REQUIRE(fx.gate.retryClose());
  REQUIRE(fx.gate.quitAuthorized());
  REQUIRE_FALSE(fx.gate.closeBlocked());
  REQUIRE(ok_spy.count() >= 1);
  REQUIRE_FALSE(fx.editor.dirty());

  auto reloaded = fx.store.load(fx.seed.id);
  REQUIRE(reloaded);
  REQUIRE(reloaded.value().content.plain_text().find("retry-then-quit") !=
          std::string::npos);

  // Authorized close may completeShutdown without deadlock.
  fx.gate.completeShutdown();
  auto* thr_retry = fx.dispatcher.workerThread();
  REQUIRE((thr_retry == nullptr || !thr_retry->isRunning()));
}

TEST_CASE("close lifecycle: clean success path flushes and authorizes quit",
          "[presentation][close][lifecycle][headless][quit][success]") {
  Fixture fx;
  fx.open_and_dirty(QStringLiteral("<p>clean-quit-content</p>"));

  QSignalSpy ok_spy(&fx.gate,
                    &notes::presentation::AppExitGate::closeSucceeded);
  REQUIRE(fx.gate.requestClose());
  REQUIRE(fx.gate.quitAuthorized());
  REQUIRE_FALSE(fx.gate.closeBlocked());
  REQUIRE(ok_spy.count() >= 1);
  REQUIRE_FALSE(fx.editor.dirty());

  auto reloaded = fx.store.load(fx.seed.id);
  REQUIRE(reloaded);
  REQUIRE(reloaded.value().content.plain_text().find("clean-quit-content") !=
          std::string::npos);

  fx.gate.completeShutdown();
  auto* thr_ok = fx.dispatcher.workerThread();
  REQUIRE((thr_ok == nullptr || !thr_ok->isRunning()));
}

TEST_CASE("close lifecycle: empty editor authorizes quit without save",
          "[presentation][close][lifecycle][headless][quit][empty]") {
  Fixture fx;
  REQUIRE(fx.gate.requestClose());
  REQUIRE(fx.gate.quitAuthorized());
  REQUIRE_FALSE(fx.gate.closeBlocked());
}
