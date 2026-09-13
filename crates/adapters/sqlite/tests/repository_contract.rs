//! Repository contract suite for the SQLite adapter (dev-dependency:
//! `rusty-notes-test-support`). The real backend must pass the exact same
//! behavioral contract as the in-memory fake, including the thread-6
//! soft-delete/purge lifecycle and the thread-7 missing-folder parity case.

use rusty_notes_sqlite::SqliteNoteRepository;
use rusty_notes_test_support::contracts::run_all;

/// The shared repository contract against the real backend.
#[test]
fn sqlite_repository_honors_the_contract() {
    run_all(|| SqliteNoteRepository::open_in_memory().expect("in-memory db"));
}

// ---------------- SQLite-specific repository assertions ----------------
// (error-value-level pins beyond the generic contract cases)

use rusty_notes_application::error::RepoError;
use rusty_notes_application::ports::NoteRepository;
use rusty_notes_domain::{Folder, FolderId, Note, NoteDraft, NoteId, Timestamp};

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

fn temp_repo() -> SqliteNoteRepository {
    SqliteNoteRepository::open_in_memory().expect("in-memory db")
}

#[test]
fn soft_delete_hides_note_from_listings_and_updates() {
    let repo = temp_repo();
    let f = folder(&repo, "f1", "Inbox");
    let _n = note(&repo, &f, "n1", "doomed", "");
    repo.soft_delete_note(&NoteId("n1".into()))
        .expect("soft delete");

    assert!(repo.get_note(&NoteId("n1".into())).expect("get").is_none());
    assert!(repo.list_all_notes().expect("list").is_empty());
    assert!(repo
        .list_notes_by_folder(&FolderId("f1".into()))
        .expect("list")
        .is_empty());

    // Second delete: explicit NotFound.
    let err = repo
        .soft_delete_note(&NoteId("n1".into()))
        .expect_err("second delete");
    assert_eq!(err, RepoError::NotFound);
}

#[test]
fn folder_delete_restriction_includes_soft_deleted_notes() {
    let repo = temp_repo();
    let f = folder(&repo, "f1", "Has notes");
    let empty = folder(&repo, "f2", "Empty");
    let n = note(&repo, &f, "n1", "t", "");

    let err = repo.delete_folder(&f.id).expect_err("has notes");
    assert_eq!(err, RepoError::FolderHasNotes);

    repo.soft_delete_note(&n.id).expect("soft delete");
    let err = repo
        .delete_folder(&f.id)
        .expect_err("has soft-deleted note");
    assert_eq!(err, RepoError::FolderHasNotes);

    repo.delete_folder(&empty.id).expect("empty folder deletes");
    assert!(repo.get_folder(&empty.id).expect("get").is_none());
}
