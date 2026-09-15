#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/delete_note.hpp"
#include "application/use_cases/notes/list_notes.hpp"
#include "application/use_cases/notes/list_trashed_notes.hpp"
#include "application/use_cases/notes/move_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/restore_note.hpp"
#include "application/use_cases/notes/search_notes.hpp"
#include "domain/notes/note_content.hpp"
#include "presentation/qt/view_models/note_list_view_model.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

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
  notes::testing::FixedClock clock{80'000};
  notes::application::ListNotes list{store};
  notes::application::CreateNote create{store, clock};
  notes::application::DeleteNote del{store, clock};
  notes::application::SearchNotes search{store};
  notes::application::ListTrashedNotes list_trashed{store};
  notes::application::RestoreNote restore{store, store, store};
  notes::application::PurgeNote purge{store, store, nullptr};
  notes::application::MoveNote move{store, store, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::NoteListViewModel vm{
      list, create, del, search, list_trashed, restore, purge, move, dispatcher};

  notes::domain::Folder folder_a;
  notes::domain::Folder folder_b;
  notes::domain::Note pinned;
  notes::domain::Note unpinned;
  notes::domain::Note other_folder;

  Fixture() {
    folder_a.id = notes::domain::FolderId{"a"};
    folder_a.name = "A";
    folder_b.id = notes::domain::FolderId{"b"};
    folder_b.name = "B";
    REQUIRE(store.save(folder_a));
    REQUIRE(store.save(folder_b));

    pinned.id = notes::domain::NoteId{"pin-1"};
    pinned.folder_id = folder_a.id;
    pinned.title = "Pinned alpha";
    pinned.content = notes::domain::NoteContent::from_plain_text("needle body");
    pinned.pinned = true;
    pinned.revision = 0;
    pinned.created_at_ms = 1;
    pinned.modified_at_ms = 100;
    REQUIRE(store.save(pinned));
    pinned = store.load(pinned.id).value();

    unpinned.id = notes::domain::NoteId{"plain-1"};
    unpinned.folder_id = folder_a.id;
    unpinned.title = "Zulu plain";
    unpinned.content = notes::domain::NoteContent::from_plain_text("other");
    unpinned.pinned = false;
    unpinned.revision = 0;
    unpinned.created_at_ms = 1;
    unpinned.modified_at_ms = 200;
    REQUIRE(store.save(unpinned));
    unpinned = store.load(unpinned.id).value();

    other_folder.id = notes::domain::NoteId{"b-1"};
    other_folder.folder_id = folder_b.id;
    other_folder.title = "needle elsewhere";
    other_folder.content =
        notes::domain::NoteContent::from_plain_text("needle other folder");
    other_folder.pinned = false;
    other_folder.revision = 0;
    other_folder.created_at_ms = 1;
    other_folder.modified_at_ms = 50;
    REQUIRE(store.save(other_folder));
  }

  ~Fixture() { dispatcher.shutdown(); }
};

}  // namespace

TEST_CASE("search excludes trashed notes",
          "[presentation][search][trash][coherence]") {
  Fixture f;
  f.vm.setFolderId(QString::fromStdString(f.folder_a.id.value()));
  pump();

  f.vm.setSearchQuery(QStringLiteral("needle"));
  pump();
  REQUIRE(f.vm.searching());
  REQUIRE(f.vm.model()->rowCount() >= 1);
  // Includes folder-a pinned + folder-b other; no trash yet.
  const int before = f.vm.model()->rowCount();
  REQUIRE(before >= 2);

  // Trash via VM while searching: row drops immediately; store excludes from
  // subsequent search.
  f.vm.trashNote(QString::fromStdString(f.pinned.id.value()));
  pump();
  for (int r = 0; r < f.vm.model()->rowCount(); ++r) {
    REQUIRE(f.vm.model()->noteIdAt(r) != f.pinned.id);
  }
  REQUIRE(f.vm.model()->rowCount() == before - 1);

  // Force a fresh search (same text) after clear.
  f.vm.setSearchQuery({});
  pump();
  f.vm.setSearchQuery(QStringLiteral("needle"));
  pump();
  for (int r = 0; r < f.vm.model()->rowCount(); ++r) {
    REQUIRE(f.vm.model()->noteIdAt(r) != f.pinned.id);
  }
  REQUIRE(f.store.search("needle").value().size() ==
          static_cast<std::size_t>(f.vm.model()->rowCount()));
}

TEST_CASE("pin sort after move and restore",
          "[presentation][pin][move][coherence]") {
  Fixture f;
  f.vm.setFolderId(QString::fromStdString(f.folder_a.id.value()));
  pump();
  REQUIRE(f.vm.model()->rowCount() == 2);
  REQUIRE(f.vm.model()->noteIdAt(0) == f.pinned.id);

  f.vm.moveNote(QString::fromStdString(f.pinned.id.value()),
                QString::fromStdString(f.folder_b.id.value()));
  pump();
  // Moved away from folder A list.
  REQUIRE(f.vm.model()->rowCount() == 1);
  REQUIRE(f.vm.model()->noteIdAt(0) == f.unpinned.id);

  f.vm.setFolderId(QString::fromStdString(f.folder_b.id.value()));
  pump();
  REQUIRE(f.vm.model()->rowCount() == 2);
  // Pinned still first in destination.
  REQUIRE(f.vm.model()->noteIdAt(0) == f.pinned.id);

  f.vm.trashNote(QString::fromStdString(f.pinned.id.value()));
  pump();
  f.vm.showTrash();
  pump();
  REQUIRE(f.vm.showingTrash());
  REQUIRE(f.vm.model()->rowCount() >= 1);

  f.vm.restoreNote(QString::fromStdString(f.pinned.id.value()));
  pump();
  f.vm.hideTrash();
  f.vm.setFolderId(QString::fromStdString(f.folder_b.id.value()));
  pump();
  // Restore returns to prior folder b; pin order intact.
  bool found = false;
  for (int r = 0; r < f.vm.model()->rowCount(); ++r) {
    if (f.vm.model()->noteIdAt(r) == f.pinned.id) {
      found = true;
      REQUIRE(r == 0);
      REQUIRE(f.vm.model()
                  ->data(f.vm.model()->index(r, 0),
                         notes::presentation::NoteListModel::PinnedRole)
                  .toBool());
    }
  }
  REQUIRE(found);
}

TEST_CASE("selection cleared after trash and folder switch",
          "[presentation][selection][coherence]") {
  Fixture f;
  f.vm.setFolderId(QString::fromStdString(f.folder_a.id.value()));
  pump();
  const QString pid = QString::fromStdString(f.pinned.id.value());
  f.vm.setSelectedNoteId(pid);
  REQUIRE(f.vm.selectedNoteId() == pid);

  QSignalSpy sel_spy(&f.vm,
                     &notes::presentation::NoteListViewModel::selectedNoteIdChanged);
  f.vm.trashNote(pid);
  pump();
  REQUIRE(f.vm.selectedNoteId().isEmpty());
  REQUIRE(sel_spy.count() >= 1);

  f.vm.setSelectedNoteId(QString::fromStdString(f.unpinned.id.value()));
  REQUIRE_FALSE(f.vm.selectedNoteId().isEmpty());
  f.vm.setFolderId(QString::fromStdString(f.folder_b.id.value()));
  pump();
  REQUIRE(f.vm.selectedNoteId().isEmpty());
}

TEST_CASE("trash view selectable and search leaves trash mode",
          "[presentation][trash][search][coherence]") {
  Fixture f;
  f.vm.setFolderId(QString::fromStdString(f.folder_a.id.value()));
  pump();
  f.vm.trashNote(QString::fromStdString(f.pinned.id.value()));
  pump();
  f.vm.showTrash();
  pump();
  REQUIRE(f.vm.showingTrash());
  REQUIRE(f.vm.model()->rowCount() >= 1);
  REQUIRE(f.vm.selectedNoteId().isEmpty());

  f.vm.setSearchQuery(QStringLiteral("Zulu"));
  pump();
  REQUIRE_FALSE(f.vm.showingTrash());
  REQUIRE(f.vm.searching());
  // Unpinned still active and searchable; pinned trashed excluded.
  REQUIRE(f.vm.model()->rowCount() == 1);
  REQUIRE(f.vm.model()->noteIdAt(0) == f.unpinned.id);
}

TEST_CASE("applySummaryTitle does not resurrect trashed into list",
          "[presentation][pin][coherence]") {
  Fixture f;
  f.vm.setFolderId(QString::fromStdString(f.folder_a.id.value()));
  pump();
  f.vm.trashNote(QString::fromStdString(f.pinned.id.value()));
  pump();
  REQUIRE(f.vm.model()->rowCount() == 1);
  f.vm.applySummaryTitle(QString::fromStdString(f.pinned.id.value()),
                         QStringLiteral("ghost"), 9, true);
  REQUIRE(f.vm.model()->rowCount() == 1);
  REQUIRE(f.vm.model()->noteIdAt(0) == f.unpinned.id);
}
