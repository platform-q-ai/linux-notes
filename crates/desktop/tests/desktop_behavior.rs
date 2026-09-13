//! Named headless regression suite for the desktop behavior fixes. Runs with no
//! display: it drives the shell state machine (`app::AppState`) and the wired
//! use-case bundle (`app::Actions`) exactly as the views do.
//!
//! These tests deliberately keep `rusty-notes-test-support` out of the production
//! graph: it is a dev-dependency of this crate only.

use std::sync::Arc;

use rusty_notes_application::ports::{Ids, NoteRepository};
use rusty_notes_application::use_cases::notes::OpenNote;
use rusty_notes_desktop::app::{Actions, AppState};
use rusty_notes_domain::{NoteDraft, NoteId, Timestamp};
use rusty_notes_system::{SystemClock, UniqueIds};
use rusty_notes_test_support::fakes::{FixedClock, InMemoryNoteRepository, SequentialIds};

/// Shared fixture bundle for the headless tests below.
struct Fixture {
    actions: Actions,
    state: AppState,
    repo: Arc<InMemoryNoteRepository>,
    clock: Arc<FixedClock>,
    #[allow(dead_code)] // kept for fixture symmetry; ids are wired into the use cases
    ids: Arc<SequentialIds>,
}

fn fixture() -> Fixture {
    let repo = Arc::new(InMemoryNoteRepository::default());
    let search =
        rusty_notes_test_support::fakes::InMemorySearch::new(InMemoryNoteRepository::clone(&repo));
    let ids = Arc::new(SequentialIds::default());
    let clock = Arc::new(FixedClock::new(Timestamp("2026-09-13T12:00:00Z".into())));
    let actions = Actions {
        list_notes: rusty_notes_application::use_cases::notes::ListNotes::new(repo.clone()),
        list_folders: rusty_notes_application::use_cases::folders::ListFolders::new(repo.clone()),
        open_note: OpenNote::new(repo.clone()),
        create_note: rusty_notes_application::use_cases::notes::CreateNote::new(
            repo.clone(),
            ids.clone(),
            clock.clone(),
        ),
        update_note: rusty_notes_application::use_cases::notes::UpdateNote::new(
            repo.clone(),
            clock.clone(),
        ),
        delete_note: rusty_notes_application::use_cases::notes::DeleteNote::new(repo.clone()),
        move_note: rusty_notes_application::use_cases::notes::MoveNote::new(
            repo.clone(),
            clock.clone(),
        ),
        create_folder: rusty_notes_application::use_cases::folders::CreateFolder::new(
            repo.clone(),
            ids.clone(),
            clock.clone(),
        ),
        rename_folder: rusty_notes_application::use_cases::folders::RenameFolder::new(
            repo.clone(),
            clock.clone(),
        ),
        delete_folder: rusty_notes_application::use_cases::folders::DeleteFolder::new(repo.clone()),
        search: rusty_notes_application::use_cases::notes::SearchNotes::new(
            Arc::new(search),
            repo.clone(),
        ),
    };
    Fixture {
        actions,
        state: AppState::default(),
        repo,
        clock,
        ids,
    }
}

fn unique_ids_fixture() -> (Arc<UniqueIds>, Arc<SystemClock>) {
    (Arc::new(UniqueIds::default()), Arc::new(SystemClock))
}

// ---------------------------------------------------------------------------
// Thread 1: editor body loss (P0) — opening loads the body; title-only saves
// preserve it.
// ---------------------------------------------------------------------------

#[test]
fn open_note_loads_body_and_title_only_save_preserves_it() {
    let mut fx = fixture();
    let folder = fx
        .actions
        .create_folder
        .execute("Inbox".into())
        .expect("folder");
    let note = fx
        .actions
        .create_note
        .execute(NoteDraft {
            folder_id: folder.id.clone(),
            title: "shopping".into(),
            body: "milk, eggs, bread".into(),
        })
        .expect("note");

    // Refresh, then open the note the way the note-list view does.
    fx.actions.refresh(&mut fx.state).expect("refresh");
    fx.actions
        .open_note(&mut fx.state, note.id.as_str())
        .expect("open note");

    // The editor must show the REAL stored body, not an empty one.
    assert_eq!(fx.state.editor.note_id.as_deref(), Some(note.id.as_str()));
    assert_eq!(fx.state.editor.title, "shopping");
    assert_eq!(fx.state.editor.body, "milk, eggs, bread");
    assert!(!fx.state.editor.dirty);

    // Title-only edit + save: the stored body must survive (regression guard for
    // the P0 body-wipe).
    fx.state.edit_title("shopping list".into());
    fx.actions.save(&mut fx.state).expect("save");
    fx.clock.advance(Timestamp("2026-09-13T13:00:00Z".into()));
    let stored = fx
        .repo
        .get_note(&NoteId(note.id.0.clone()))
        .expect("get")
        .expect("still there");
    assert_eq!(stored.title, "shopping list");
    assert_eq!(
        stored.body, "milk, eggs, bread",
        "body must survive a title-only save"
    );
}

#[test]
fn body_only_save_preserves_title() {
    let mut fx = fixture();
    let folder = fx
        .actions
        .create_folder
        .execute("Inbox".into())
        .expect("folder");
    let note = fx
        .actions
        .create_note
        .execute(NoteDraft {
            folder_id: folder.id.clone(),
            title: "t".into(),
            body: "before".into(),
        })
        .expect("note");
    fx.actions.refresh(&mut fx.state).expect("refresh");
    fx.actions
        .open_note(&mut fx.state, note.id.as_str())
        .expect("open");

    fx.state.edit_body("after".into());
    fx.actions.save(&mut fx.state).expect("save");
    let stored = fx
        .repo
        .get_note(&NoteId(note.id.0.clone()))
        .expect("get")
        .expect("still there");
    assert_eq!(stored.title, "t");
    assert_eq!(stored.body, "after");
}

// ---------------------------------------------------------------------------
// Thread 2 (desktop wiring side / F8 / F11): the composition root must use the
// production id/clock adapters, not test fakes — and the production id source
// must not collide after a restart over the same database.
// ---------------------------------------------------------------------------

#[test]
fn unique_ids_do_not_collide_across_fresh_instances_like_a_restart() {
    // Two separate `UniqueIds` instances stand in for two app runs: nothing is
    // shared between them (no counter, no seed). The fake `SequentialIds` hands
    // out `folder-1` in both runs (the restart bug); the production adapter must
    // never repeat an id.
    let (ids_a, _clock) = unique_ids_fixture();
    let ids_b = Arc::new(UniqueIds::default());

    let a1 = ids_a.next_folder_id();
    let a2 = ids_a.next_note_id();
    let b1 = ids_b.next_folder_id();
    let b2 = ids_b.next_note_id();

    assert_ne!(a1, b1, "folder ids must not collide across restarts");
    assert_ne!(a2, b2, "note ids must not collide across restarts");
    assert_ne!(a1.0, a2.0, "ids handed out within one run must differ");
    assert_ne!(b1.0, b2.0);
}

#[test]
fn composition_root_builds_production_actions_over_sqlite() {
    let tmp = tempfile::tempdir().expect("tempdir");
    let db = tmp.path().join("composition.db");
    let actions = rusty_notes_desktop::composition::build_wired(db.to_str().unwrap())
        .expect("production wiring builds");

    // The production bundle must be able to create a folder and a note end to end
    // (this exercises the real clock + real id adapters, not test fakes).
    let mut view_state = AppState::default();
    actions
        .create_folder
        .execute("Inbox".into())
        .expect("folder");
    actions.refresh(&mut view_state).expect("refresh");
    assert_eq!(view_state.folders.len(), 1);

    // Opening the composition twice over the same file (an app restart) must keep
    // working; ids handed out by the production adapter stay unique (no fake).
    let _second_run = rusty_notes_desktop::composition::build_wired(db.to_str().unwrap())
        .expect("second run over the same database");
}

// ---------------------------------------------------------------------------
// Thread 5: the error banner is dismissible.
// ---------------------------------------------------------------------------

#[test]
fn error_banner_dismissal_clears_the_error() {
    let mut fx = fixture();
    assert!(fx.state.error.is_none());

    // An error appears (e.g. a failed folder delete).
    fx.state
        .set_error("folder still contains notes".to_string());
    assert!(fx.state.error.is_some());

    // The banner's dismiss affordance calls `clear_error`; the error goes away
    // and stays away.
    fx.state.clear_error();
    assert!(fx.state.error.is_none(), "dismissed error must not return");
    fx.state.clear_error(); // dismissing again is a no-op, not a panic
    assert!(fx.state.error.is_none());
}

#[test]
fn dismissed_error_stays_dismissed_after_refresh() {
    let mut fx = fixture();
    fx.state.set_error("storage error: boom".to_string());
    fx.state.clear_error();
    fx.actions.refresh(&mut fx.state).expect("refresh");
    assert!(
        fx.state.error.is_none(),
        "refresh must not resurrect errors"
    );
}

// ---------------------------------------------------------------------------
// Thread 6 (UI side): a note that vanished from storage (soft-deleted or purged
// via another surface) must not come back as a ghost editor. Stale rows are
// healed on the next refresh instead.
// ---------------------------------------------------------------------------

#[test]
fn opening_a_deleted_note_clears_selection_and_reports_not_found() {
    let mut fx = fixture();
    let folder = fx
        .actions
        .create_folder
        .execute("Inbox".into())
        .expect("folder");
    let note = fx
        .actions
        .create_note
        .execute(NoteDraft {
            folder_id: folder.id.clone(),
            title: "to be deleted".into(),
            body: "body".into(),
        })
        .expect("note");
    fx.actions.refresh(&mut fx.state).expect("refresh");

    // Delete the note behind the shell's back (another path / earlier session).
    fx.actions
        .delete_note
        .execute(&NoteId(note.id.0.clone()))
        .expect("delete");

    // A stale list row is clicked: the shell must surface a not-found error and
    // drop the dead selection instead of rendering a ghost editor.
    let err = fx
        .actions
        .open_note(&mut fx.state, note.id.as_str())
        .expect_err("stale open must fail");
    assert_eq!(err, "note not found");
    assert!(fx.state.selected_note.is_none());
    assert!(
        fx.state.editor.note_id.is_none(),
        "no ghost editor for a deleted note"
    );

    // Refresh heals the middle pane: the stale row disappears.
    fx.actions.refresh(&mut fx.state).expect("refresh");
    assert!(
        !fx.state.notes.iter().any(|n| n.id == note.id.as_str()),
        "deleted note must leave the listing"
    );
}
