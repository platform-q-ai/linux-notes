// SQLite-backed trash / restore / purge + search exclusion + structured body keep.
#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "application/use_cases/notes/move_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/restore_note.hpp"
#include "application/use_cases/notes/trash_note.hpp"
#include "adapters/persistence/sqlite/sqlite_folder_store.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_attachment_store.hpp"
#include "tests/support/require.hpp"
#include "tests/support/structured_fixtures.hpp"
#include "tests/support/temp_db.hpp"

#include <iostream>
#include <memory>

namespace {

using notes::testing::require;

struct Env {
  notes::testing::TempDbPath path;
  std::shared_ptr<notes::adapters::persistence::SqliteDb> db;
  notes::adapters::persistence::SqliteNoteStore notes;
  notes::adapters::persistence::SqliteFolderStore folders;
  notes::testing::FixedClock clock;

  explicit Env(const char* name)
      : path(name),
        db(std::make_shared<notes::adapters::persistence::SqliteDb>()),
        notes(db),
        folders(db),
        clock(10'000) {
    require(static_cast<bool>(db->open(path.path())), "open");
  }

  notes::domain::Note seed_structured(const char* id = "n1") {
    auto n = notes::testing::make_structured_note(id, "root");
    auto s = notes.save(n);
    require(s.has_value(), "seed save");
    return s.value();
  }
};

void test_trash_hides_from_list_and_search() {
  Env env("trash-hide.db");
  auto n = env.seed_structured("hide-1");
  notes::application::TrashNote trash{env.notes, env.clock};
  require(trash.execute(n.id).has_value(), "trash");

  auto list = env.notes.list(notes::domain::FolderId{std::string{"root"}});
  require(list.has_value() && list.value().empty(), "list excludes trash");

  auto hits = env.notes.search("Structured");
  require(hits.has_value() && hits.value().empty(), "search excludes trash");

  auto trashed = env.notes.list_trashed();
  require(trashed.has_value() && trashed.value().size() == 1, "list_trashed");
  require(trashed.value()[0].id == n.id, "trashed id");

  auto loaded = env.notes.load(n.id);
  require(loaded.has_value() && loaded.value().is_trashed(), "still loadable");
  require(notes::testing::content_has_checklist_and_attachment(loaded.value().content),
          "body kept in trash");
}

// F1 regression: trash must be durable against a stale editor body-save that still
// carries pre-trash revision and trashed_at_ms=0 (would otherwise clear trash cols).
void test_stale_body_save_cannot_resurrect_trashed_note() {
  Env env("trash-stale-save.db");
  auto saved = env.seed_structured("resurrect-1");
  const auto pre_trash_rev = saved.revision;
  require(pre_trash_rev > 0, "seeded revision");

  notes::application::TrashNote trash{env.notes, env.clock};
  require(trash.execute(saved.id).has_value(), "trash");

  auto after_trash = env.notes.load(saved.id);
  require(after_trash.has_value() && after_trash.value().is_trashed(),
          "trashed before stale save");
  const auto trashed_at = after_trash.value().trashed_at_ms;
  require(trashed_at > 0, "trashed_at set");
  require(after_trash.value().revision == pre_trash_rev + 1,
          "trash bumps revision");

  // Simulate editor flush with pre-trash snapshot (active trashed_at, same rev).
  notes::domain::Note stale = saved;
  stale.title = "stale body save";
  stale.content = notes::domain::NoteContent::from_plain_text("should not untrash");
  stale.modified_at_ms = trashed_at + 1;
  stale.revision = pre_trash_rev;
  stale.trashed_at_ms = 0;
  stale.trashed_from_folder_id = std::nullopt;

  auto body_save = env.notes.save(stale);
  // Must not succeed in clearing trash. Conflict or preserving trash is OK;
  // resurrection (active row / missing from trash list) is the bug.
  auto loaded = env.notes.load(saved.id);
  require(loaded.has_value(), "row still present");
  require(loaded.value().is_trashed(), "stale save must not clear trash");
  require(loaded.value().trashed_at_ms == trashed_at, "trashed_at preserved");
  require(loaded.value().revision == pre_trash_rev + 1,
          "revision stays post-trash after rejected save");

  auto list = env.notes.list(notes::domain::FolderId{std::string{"root"}});
  require(list.has_value(), "list ok");
  for (const auto& s : list.value()) {
    require(!(s.id == saved.id), "must not reappear in active list");
  }
  auto trash_list = env.notes.list_trashed();
  require(trash_list.has_value() && trash_list.value().size() == 1,
          "still in trash list");
  require(trash_list.value()[0].id == saved.id, "trash id");

  require(!body_save.has_value(), "stale pre-trash save rejected");
  require(body_save.error().kind ==
              notes::application::ErrorKind::RevisionConflict,
          "rejected save is revision conflict");

  // Defense in depth: even with post-trash revision, clearing trash via save fails.
  notes::domain::Note clear_attempt = after_trash.value();
  clear_attempt.title = "clear trash attempt";
  clear_attempt.trashed_at_ms = 0;
  clear_attempt.trashed_from_folder_id = std::nullopt;
  auto clear_save = env.notes.save(clear_attempt);
  require(!clear_save.has_value(), "cannot clear trash via save");
  require(clear_save.error().kind ==
              notes::application::ErrorKind::ValidationFailed,
          "clear-via-save is validation failure");
  auto still = env.notes.load(saved.id);
  require(still.has_value() && still.value().is_trashed(),
          "still trashed after clear attempt");
}

void test_restore_returns_to_prior_folder() {
  Env env("trash-restore.db");
  // Create secondary folder
  notes::domain::Folder f;
  f.id = notes::domain::FolderId{std::string{"work"}};
  f.name = "Work";
  f.parent_id = notes::domain::FolderId{std::string{"root"}};
  f.sort_order = 1;
  require(env.folders.save(f).has_value(), "folder");

  auto n = notes::testing::make_structured_note("r1", "work");
  auto saved = env.notes.save(n);
  require(saved.has_value(), "save in work");

  notes::application::TrashNote trash{env.notes, env.clock};
  require(trash.execute(saved.value().id).has_value(), "trash");

  notes::application::RestoreNote restore{env.notes, env.notes, env.folders};
  auto back = restore.execute(saved.value().id);
  require(back.has_value(), "restore");
  require(!back.value().is_trashed(), "not trashed");
  require(back.value().folder_id.value() == "work", "back to work");
  require(notes::testing::content_has_checklist_and_attachment(back.value().content),
          "structure after restore");
}

void test_purge_requires_trash_and_removes_row() {
  Env env("trash-purge.db");
  auto n = env.seed_structured("p1");
  notes::application::PurgeNote purge{env.notes, env.notes, nullptr};
  auto early = purge.execute(n.id);
  require(!early.has_value(), "purge without trash fails");

  notes::application::TrashNote trash{env.notes, env.clock};
  require(trash.execute(n.id).has_value(), "trash first");
  require(purge.execute(n.id).has_value(), "purge ok");
  auto gone = env.notes.load(n.id);
  require(!gone.has_value(), "gone");
  auto trashed = env.notes.list_trashed();
  require(trashed.has_value() && trashed.value().empty(), "trash empty");
}

void test_purge_gc_attachments() {
  Env env("trash-purge-att.db");
  notes::testing::InMemoryAttachmentStore atts{env.clock};
  auto put = atts.put(notes::domain::NoteId{std::string{"pg"}}, "x.bin",
                      "application/octet-stream", {1, 2, 3});
  require(put.has_value(), "put att");

  auto n = notes::testing::make_structured_note("pg", "root");
  n.content = notes::testing::make_mixed_structured_content(
      put.value().id.value(), "x.bin");
  auto saved = env.notes.save(n);
  require(saved.has_value(), "save");

  notes::application::TrashNote trash{env.notes, env.clock};
  require(trash.execute(saved.value().id).has_value(), "trash");
  notes::application::PurgeNote purge{env.notes, env.notes, &atts};
  require(purge.execute(saved.value().id).has_value(), "purge");
  auto missing = atts.get(put.value().id);
  require(!missing.has_value(), "attachment GC");
}

void test_move_active_note() {
  Env env("move-note.db");
  notes::domain::Folder f;
  f.id = notes::domain::FolderId{std::string{"dest"}};
  f.name = "Dest";
  f.parent_id = notes::domain::FolderId{std::string{"root"}};
  require(env.folders.save(f).has_value(), "dest folder");

  auto n = env.seed_structured("m1");
  notes::application::MoveNote move{env.notes, env.notes, env.folders, env.clock};
  notes::application::MoveNote::Request req;
  req.note_id = n.id;
  req.target_folder_id = f.id;
  auto moved = move.execute(req);
  require(moved.has_value(), "move");
  require(moved.value().folder_id == f.id, "folder");

  // Cannot move trashed
  notes::application::TrashNote trash{env.notes, env.clock};
  require(trash.execute(n.id).has_value(), "trash");
  auto bad = move.execute(req);
  require(!bad.has_value(), "no move trash");
}

void test_restore_fallback_when_folder_gone() {
  Env env("restore-fallback.db");
  notes::domain::Folder f;
  f.id = notes::domain::FolderId{std::string{"ephemeral"}};
  f.name = "E";
  f.parent_id = notes::domain::FolderId{std::string{"root"}};
  require(env.folders.save(f).has_value(), "folder");

  auto n = notes::testing::make_structured_note("fb1", "ephemeral");
  auto saved = env.notes.save(n);
  require(saved.has_value(), "save");
  notes::application::TrashNote trash{env.notes, env.clock};
  require(trash.execute(saved.value().id).has_value(), "trash");

  // Remove folder while note trashed (adapter may refuse if notes present —
  // trashed notes often reparented to root; if folder still referenced, force SQL).
  // Prefer store remove if allowed; else raw delete after checking.
  auto rm = env.folders.remove(f.id);
  if (!rm) {
    // Force-delete folder row for fallback scenario
    require(static_cast<bool>(env.db->exec(
                "DELETE FROM folders WHERE id='ephemeral';")),
            "force drop folder");
  }

  notes::application::RestoreNote restore{env.notes, env.notes, env.folders};
  auto back = restore.execute(saved.value().id);
  require(back.has_value(), "restore fallback");
  require(back.value().folder_id.value() == "root", "fallback root");
  require(!back.value().is_trashed(), "active");
}

}  // namespace

int main() {
  try {
    test_trash_hides_from_list_and_search();
    test_stale_body_save_cannot_resurrect_trashed_note();
    test_restore_returns_to_prior_folder();
    test_purge_requires_trash_and_removes_row();
    test_purge_gc_attachments();
    test_move_active_note();
    test_restore_fallback_when_folder_gone();
    std::cerr << "trash_lifecycle_sqlite_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "trash_lifecycle_sqlite_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
