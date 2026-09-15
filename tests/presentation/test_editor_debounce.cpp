#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/delete_note.hpp"
#include "application/use_cases/notes/list_notes.hpp"
#include "application/use_cases/notes/list_trashed_notes.hpp"
#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/move_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/restore_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/use_cases/notes/search_notes.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "presentation/qt/mapping/note_content_document_mapper.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"
#include "presentation/qt/view_models/note_list_view_model.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QThread>

#include <atomic>
#include <variant>

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
  [[nodiscard]] notes::application::Result<void> trash(
      const notes::domain::NoteId&, std::int64_t) override {
    return notes::application::Result<void>::ok();
  }
  [[nodiscard]] notes::application::Result<notes::domain::Note> restore(
      const notes::domain::NoteId&,
      const notes::domain::FolderId&) override {
    return notes::application::Result<notes::domain::Note>::fail(
        {notes::application::ErrorKind::NotFound, "n/a"});
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

  notes::application::LoadNote load{store};
  FailingNoteWriter fail_writer;
  notes::application::SaveNote save{fail_writer, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

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
  editor.openNote(QString::fromStdString(b.value().id.value()));
  pump(40);
  REQUIRE(editor.noteId().toStdString() == b.value().id.value());
  auto reloaded_a = store.load(a.value().id);
  REQUIRE(reloaded_a);
  REQUIRE_FALSE(reloaded_a.value().content.plain_text().find("two") !=
                std::string::npos);
  dispatcher.shutdown();
}

TEST_CASE("P3 dirty switch clears saving_ so later saves work",
          "[presentation][saving][headless][P3]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{7100};
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
  editor.openNote(QString::fromStdString(b.value().id.value()));
  pump(40);
  REQUIRE_FALSE(editor.saving());
  editor.setHtml(QStringLiteral("<p>dirty-b-final</p>"));
  REQUIRE(editor.dirty());
  editor.saveNow();
  pump(40);
  REQUIRE_FALSE(editor.dirty());
  auto reloaded_b = store.load(b.value().id);
  REQUIRE(reloaded_b);
  REQUIRE(reloaded_b.value().content.plain_text().find("dirty-b-final") !=
          std::string::npos);
  dispatcher.shutdown();
}

TEST_CASE("P4 closeNote flushes dirty content",
          "[presentation][close][dirty][headless][P4]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{7200};
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
  REQUIRE(a);
  editor.openNote(QString::fromStdString(a.value().id.value()));
  pump();
  editor.setHtml(QStringLiteral("<p>must-persist-on-close</p>"));
  REQUIRE(editor.dirty());
  editor.closeNote();
  pump(10);
  REQUIRE(editor.noteId().isEmpty());
  auto reloaded = store.load(a.value().id);
  REQUIRE(reloaded);
  REQUIRE(reloaded.value().content.plain_text().find("must-persist-on-close") !=
          std::string::npos);
  dispatcher.shutdown();
}

TEST_CASE("P5 flushSync checks SaveNote Result and keeps dirty on failure",
          "[presentation][flush][headless][P5]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{7300};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::application::LoadNote load{store};
  FailingNoteWriter fail_writer;
  notes::application::SaveNote save{fail_writer, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

  notes::domain::Note seed;
  seed.id = notes::domain::NoteId{"note-flush-1"};
  seed.folder_id = folder.id;
  seed.title = "t";
  seed.content = notes::domain::NoteContent::from_plain_text("x");
  seed.revision = 0;
  seed.created_at_ms = 1;
  seed.modified_at_ms = 1;
  REQUIRE(store.save(seed));

  editor.openNote(QString::fromStdString(seed.id.value()));
  pump();
  editor.setHtml(QStringLiteral("<p>quit-edit</p>"));
  REQUIRE(editor.dirty());
  REQUIRE_FALSE(editor.flushSync());
  REQUIRE(editor.dirty());
  REQUIRE_FALSE(editor.errorString().isEmpty());
  dispatcher.shutdown();
}

TEST_CASE("P2 toggleInlineStyle applies bold without literal tags",
          "[presentation][richtext][headless][P2]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{7400};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::application::LoadNote load{store};
  notes::application::SaveNote save{store, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

  notes::domain::Note seed;
  seed.id = notes::domain::NoteId{"note-rt-1"};
  seed.folder_id = folder.id;
  seed.title = "t";
  seed.content = notes::domain::NoteContent::from_plain_text("hello world");
  seed.revision = 0;
  seed.created_at_ms = 1;
  seed.modified_at_ms = 1;
  REQUIRE(store.save(seed));

  editor.openNote(QString::fromStdString(seed.id.value()));
  pump();

  QTextDocument doc;
  doc.setHtml(editor.html());
  QTextCursor cur(&doc);
  cur.movePosition(QTextCursor::Start);
  cur.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 5);
  const int start = cur.selectionStart();
  const int end = cur.selectionEnd();
  editor.toggleInlineStyle(start, end, QStringLiteral("bold"));

  REQUIRE_FALSE(editor.html().contains(QStringLiteral("&lt;b&gt;")));
  REQUIRE_FALSE(editor.html().contains(QStringLiteral("<b>hello</b>")));

  const auto content =
      notes::presentation::NoteContentDocumentMapper::fromHtml(editor.html());
  bool bold_ok = false;
  for (const auto& block : content.blocks()) {
    const auto* p = std::get_if<notes::domain::ParagraphBlock>(&block);
    if (!p) continue;
    for (const auto& s : p->spans) {
      if (s.bold && s.text.find("hello") != std::string::npos) bold_ok = true;
      REQUIRE(s.text.find("<b>") == std::string::npos);
    }
  }
  REQUIRE(bold_ok);
  dispatcher.shutdown();
}

TEST_CASE("P8 IoWorker lives on worker thread without parent affinity crash",
          "[presentation][dispatcher][headless][P8]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::presentation::UseCaseDispatcher dispatcher;
  REQUIRE(dispatcher.workerThread() != nullptr);
  REQUIRE(dispatcher.workerThread()->isRunning());

  std::atomic<bool> ran{false};
  QThread* observed = nullptr;
  dispatcher.post([&] {
    observed = QThread::currentThread();
    ran = true;
  });
  for (int i = 0; i < 100 && !ran.load(); ++i) {
    QTest::qWait(10);
  }
  REQUIRE(ran.load());
  REQUIRE(observed == dispatcher.workerThread());
  REQUIRE(observed != QCoreApplication::instance()->thread());
  dispatcher.shutdown();
  REQUIRE_FALSE(dispatcher.workerThread()->isRunning());
}

TEST_CASE("P9 keepBothNotice emitted on conflict keep-both",
          "[presentation][keepboth][headless][P9]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{7500};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::application::LoadNote load{store};
  notes::application::SaveNote save{store, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher};

  notes::domain::Note seed;
  seed.id = notes::domain::NoteId{"note-kb-1"};
  seed.folder_id = folder.id;
  seed.title = "base";
  seed.content = notes::domain::NoteContent::from_plain_text("v0");
  seed.revision = 0;
  seed.created_at_ms = 1;
  seed.modified_at_ms = 1;
  auto created = store.save(seed);
  REQUIRE(created);

  editor.openNote(QString::fromStdString(seed.id.value()));
  pump();
  REQUIRE(editor.revision() == 1);

  notes::domain::Note concurrent = created.value();
  concurrent.revision = 1;
  concurrent.content = notes::domain::NoteContent::from_plain_text("other");
  concurrent.title = "other";
  REQUIRE(store.save(concurrent));

  QSignalSpy spy(&editor, &notes::presentation::EditorViewModel::keepBothNotice);
  editor.setHtml(QStringLiteral("<p>mine-conflict</p>"));
  editor.saveNow();
  pump(50);

  REQUIRE(spy.count() >= 1);
  REQUIRE(spy.front().at(0).toString().contains(QStringLiteral("kept both")));
  dispatcher.shutdown();
}

TEST_CASE("P12 createNote emits single openNoteRequested",
          "[presentation][create][headless][P12]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{7600};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::application::ListNotes list{store};
  notes::application::CreateNote create{store, clock};
  notes::application::DeleteNote del{store, clock};
  notes::application::SearchNotes search{store};
  notes::application::ListTrashedNotes list_trashed{store};
  notes::application::RestoreNote restore{store, store, store};
  notes::application::PurgeNote purge{store, store, nullptr};
  notes::application::MoveNote move{store, store, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::NoteListViewModel notes_vm{
      list, create, del, search, list_trashed, restore, purge, move,
      dispatcher};
  notes_vm.setFolderId(QString::fromStdString(folder.id.value()));
  pump(10);

  QSignalSpy open_spy(
      &notes_vm, &notes::presentation::NoteListViewModel::openNoteRequested);
  QSignalSpy created_spy(
      &notes_vm, &notes::presentation::NoteListViewModel::noteCreated);

  notes_vm.createNote();
  pump(40);

  REQUIRE(created_spy.count() == 1);
  REQUIRE(open_spy.count() == 1);
  REQUIRE(open_spy.front().at(0).toString() ==
          created_spy.front().at(0).toString());
  dispatcher.shutdown();
}
