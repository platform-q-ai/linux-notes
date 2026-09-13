//! Persistence suite for the SQLite adapter: migration idempotency, durable
//! storage across reopen, and trigger-synced FTS index maintenance. The thread-8
//! finding (assertion-free `probe_fts_state_after_update`) is closed by
//! `fts_stays_synced_through_insert_update_delete` below, which asserts the
//! index transitions with real assertions instead of printing probes.

use std::sync::{Arc, Mutex};

use rusty_notes_application::ports::{NoteRepository, SearchService};
use rusty_notes_domain::{Folder, FolderId, Note, NoteDraft, NoteId, Timestamp};
use rusty_notes_sqlite::{SqliteNoteRepository, SqliteSearchService};

fn folder(repo: &SqliteNoteRepository, id: &str, name: &str) -> Folder {
    let f = Folder::new(
        name.into(),
        FolderId(id.into()),
        Timestamp("2026-09-13T12:00:00Z".into()),
    )
    .expect("valid folder");
    repo.insert_folder(&f).expect("insert folder");
    f
}

fn note(repo: &SqliteNoteRepository, folder: &Folder, id: &str, title: &str, body: &str) -> Note {
    let n = Note::new(
        NoteDraft {
            folder_id: folder.id.clone(),
            title: title.into(),
            body: body.into(),
        },
        NoteId(id.into()),
        Timestamp("2026-09-13T12:00:00Z".into()),
    )
    .expect("valid note");
    repo.insert_note(&n).expect("insert note");
    n
}

#[test]
fn migration_is_idempotent() {
    // Re-running the runner on an already-migrated db is a no-op returning v1.
    let conn = rusty_notes_sqlite::open_in_memory().expect("db");
    let v1 = rusty_notes_sqlite::migrate(&conn).expect("migrate 1");
    let v2 = rusty_notes_sqlite::migrate(&conn).expect("migrate 2");
    assert_eq!(v1, v2);
    assert_eq!(v2, rusty_notes_sqlite::SCHEMA_VERSION);
}

#[test]
fn reopen_preserves_data() {
    let dir = tempfile::tempdir().expect("tempdir");
    let db = dir.path().join("notes.db");

    let repo = SqliteNoteRepository::open(&db).expect("open 1");
    let f = folder(&repo, "f1", "Inbox");
    note(&repo, &f, "n1", "persistent note", "survives reopen");
    drop(repo); // "close" the database

    let repo2 = SqliteNoteRepository::open(&db).expect("open 2");
    let listed = repo2.list_all_notes().expect("list after reopen");
    assert_eq!(listed.len(), 1);
    assert_eq!(listed[0].title, "persistent note");
    let folders = repo2.list_folders().expect("folders after reopen");
    assert_eq!(folders.len(), 1);
    assert_eq!(folders[0].name, "Inbox");
}

/// Replaces the assertion-free `probe_fts_state_after_update` probe (thread 8):
/// every index transition is ASSERTED, not printed.
#[test]
fn fts_stays_synced_through_insert_update_delete() {
    let conn = rusty_notes_sqlite::open_in_memory().expect("db");
    let conn = Arc::new(Mutex::new(conn));
    let repo = SqliteNoteRepository::shared(conn.clone());
    let search = SqliteSearchService::shared(conn);

    let f = folder(&repo, "f1", "Inbox");
    let n = note(&repo, &f, "n1", "quarterly budget", "numbers inside");

    let hits = search.search("budget").expect("search 1");
    assert_eq!(hits, vec![NoteId("n1".into())]);

    // UPDATE keeps the index in sync (trigger notes_au). The title still says
    // "budget", so that term keeps matching; the old body term must leave.
    let mut updated = n.clone();
    updated
        .edit_body(
            "nothing relevant now".into(),
            Timestamp("2026-09-13T13:00:00Z".into()),
        )
        .expect("body within cap");
    repo.update_note(&updated).expect("update");
    assert!(
        search.search("inside").expect("search 2").is_empty(),
        "old body content must leave the index after update"
    );
    assert_eq!(
        search.search("relevant").expect("search 3").len(),
        1,
        "new body content must enter the index after update"
    );
    assert_eq!(
        search.search("budget").expect("title still matches").len(),
        1
    );

    // A full update (title included) also leaves via the same trigger.
    let mut renamed = updated;
    renamed.title = "placeholder".into();
    repo.update_note(&renamed).expect("update 2");
    assert!(
        search.search("budget").expect("search 4").is_empty(),
        "old title content must leave the index after a full update"
    );

    // Soft-deleted rows drop out of search (deleted_at filter).
    repo.soft_delete_note(&renamed.id).expect("soft delete");
    assert!(
        search.search("relevant").expect("search 5").is_empty(),
        "soft-deleted rows must drop out of search"
    );
}
