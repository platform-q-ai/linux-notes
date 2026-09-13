//! Shared repository contract cases run by EVERY `NoteRepository` adapter
//! (the in-memory fake and the SQLite adapter alike). Test-only: see `lib.rs`.
//!
//! The cases exercise the PORT surface directly (no use cases in between), so a
//! failing case pins the adapter, not a use case. Every adapter's integration
//! suite calls [`run_all`] with a factory of fresh repositories.

use rusty_notes_application::error::RepoError;
use rusty_notes_application::ports::NoteRepository;
use rusty_notes_domain::{Folder, FolderId, Note, NoteDraft, NoteId, Timestamp};

const T0: &str = "2026-09-13T12:00:00Z";

/// Runs the full repository contract against a factory of fresh repositories.
/// `make` must return an independent repository over independent state per call.
pub fn run_all<R: NoteRepository + 'static>(make: impl FnMut() -> R) {
    let mut make = make;
    round_trip_case(&mut make);
    update_overwrite_case(&mut make);
    delete_semantics_case(&mut make);
    // Thread 6: explicit soft-delete lifecycle — recoverable through
    // `get_note_including_deleted`, removable via `purge_note`.
    deleted_note_recovery_and_purge_lifecycle_case(&mut make);
    folder_scoped_listing_case(&mut make);
    ordering_case(&mut make);
    folder_delete_restriction_case(&mut make);
    error_paths_case(&mut make);
    // Thread 7: `update_note` into a missing folder is `RepoError::NotFound` on
    // every adapter (adapter parity).
    update_note_to_missing_folder_is_not_found_case(&mut make);
    folder_rename_case(&mut make);
}

/// Inserts a folder directly through the port (fixture helper).
fn seed_folder<R: NoteRepository>(repo: &R, id: &str, name: &str) -> Folder {
    let folder = Folder::new(name.to_string(), FolderId(id.into()), Timestamp(T0.into()))
        .expect("valid folder");
    repo.insert_folder(&folder).expect("insert folder");
    folder
}

/// Inserts a note directly through the port (fixture helper).
fn seed_note<R: NoteRepository>(
    repo: &R,
    id: &str,
    folder_id: &FolderId,
    title: &str,
    body: &str,
    at: &str,
) -> Note {
    let note = Note::new(
        NoteDraft {
            folder_id: folder_id.clone(),
            title: title.into(),
            body: body.into(),
        },
        NoteId(id.into()),
        Timestamp(at.into()),
    )
    .expect("valid note");
    repo.insert_note(&note).expect("insert note");
    note
}

fn round_trip_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let repo = make();
    let f = seed_folder(&repo, "f1", "Inbox");
    let n = seed_note(&repo, "n1", &f.id, "title one", "body one", T0);
    assert_eq!(repo.get_note(&n.id).expect("get").unwrap(), n);
    let listed = repo.list_notes_by_folder(&f.id).expect("list");
    assert_eq!(listed.len(), 1);
    assert_eq!(listed[0].id, n.id);
    assert_eq!(listed[0].title, "title one");
    assert_eq!(listed[0].body, "body one");
    assert_eq!(listed[0].created_at, listed[0].updated_at);
}

fn update_overwrite_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let repo = make();
    let f = seed_folder(&repo, "f1", "Inbox");
    let n = seed_note(&repo, "n1", &f.id, "before", "old body", T0);

    // Full overwrite through the port, with a later `updated_at`.
    let mut updated = n.clone();
    updated.title = "after".into();
    updated.body = "new body".into();
    updated.updated_at = Timestamp("2026-09-13T13:00:00Z".into());
    repo.update_note(&updated).expect("update");
    assert_eq!(repo.get_note(&n.id).expect("get").unwrap(), updated);

    // Unknown note cannot be updated.
    let mut ghost = updated;
    ghost.id = NoteId("missing".into());
    assert_eq!(repo.update_note(&ghost), Err(RepoError::NotFound));
}

fn delete_semantics_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let repo = make();
    let f = seed_folder(&repo, "f1", "Inbox");
    let n = seed_note(&repo, "n1", &f.id, "doomed", "body", T0);

    repo.soft_delete_note(&n.id).expect("delete ok");

    // Deleted notes vanish from listings and cannot be updated.
    assert!(repo.list_notes_by_folder(&f.id).expect("list").is_empty());
    assert!(repo.list_all_notes().expect("list all").is_empty());
    let mut zombie = n.clone();
    zombie.title = "zombie".into();
    assert_eq!(repo.update_note(&zombie), Err(RepoError::NotFound));

    // Deleting again is NotFound: deletion is explicit, the caller learns it
    // deleted nothing.
    assert_eq!(repo.soft_delete_note(&n.id), Err(RepoError::NotFound));
}

/// Soft-delete lifecycle contract (behavior thread 6 / inline thread
/// 3999753472): `get_note` excludes soft-deleted notes, the deleted row stays
/// explicitly reachable via `get_note_including_deleted`, and `purge_note`
/// ("empty trash") removes it for good — after which a folder that once held it
/// becomes deletable (no permanently undeletable folders).
fn deleted_note_recovery_and_purge_lifecycle_case<R: NoteRepository + 'static>(
    make: &mut impl FnMut() -> R,
) {
    let repo = make();
    let f = seed_folder(&repo, "f1", "Inbox");
    let n = seed_note(&repo, "n1", &f.id, "recover me", "kept body", T0);

    repo.soft_delete_note(&n.id).expect("soft delete");

    // Plain `get_note` excludes the soft-deleted note (thread 6 doc truth)...
    assert_eq!(
        repo.get_note(&n.id).expect("get"),
        None,
        "get_note must not return soft-deleted notes"
    );
    // ...but the deleted row stays reachable through the explicit port method.
    let recovered = repo
        .get_note_including_deleted(&n.id)
        .expect("get including deleted")
        .expect("soft-deleted note is recoverable");
    assert_eq!(recovered.id, n.id);
    assert_eq!(recovered.title, "recover me");
    assert_eq!(recovered.body, "kept body");

    // Before the purge the folder still refuses to delete...
    assert_eq!(
        repo.delete_folder(&f.id),
        Err(RepoError::FolderHasNotes),
        "soft-deleted notes still reference the folder"
    );

    // ...purging ("empty trash") removes the row for good...
    repo.purge_note(&n.id).expect("purge");
    assert_eq!(
        repo.get_note_including_deleted(&n.id).expect("get"),
        None,
        "purged notes are gone through the whole port surface"
    );
    assert_eq!(repo.purge_note(&n.id), Err(RepoError::NotFound));

    // ...and the folder that once held the note is deletable again.
    repo.delete_folder(&f.id)
        .expect("folder delete after purge");
}

fn folder_scoped_listing_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let repo = make();
    let f1 = seed_folder(&repo, "f1", "One");
    let f2 = seed_folder(&repo, "f2", "Two");
    let n1 = seed_note(&repo, "n1", &f1.id, "in one", "", T0);
    let _n2 = seed_note(&repo, "n2", &f2.id, "in two", "", T0);
    let in_one = repo.list_notes_by_folder(&f1.id).expect("list f1");
    assert_eq!(in_one.len(), 1);
    assert_eq!(in_one[0].id, n1.id);
    assert_eq!(repo.list_all_notes().expect("list all").len(), 2);
}

fn ordering_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let repo = make();
    let f = seed_folder(&repo, "f1", "Inbox");
    let first = seed_note(&repo, "n1", &f.id, "first", "", "2026-09-13T12:00:01Z");
    let _second = seed_note(&repo, "n2", &f.id, "second", "", "2026-09-13T12:00:02Z");
    let _third = seed_note(&repo, "n3", &f.id, "third", "", "2026-09-13T12:00:03Z");
    // Touch `first` so it becomes the most recently updated.
    let mut touched = first.clone();
    touched.title = "first!".into();
    touched.updated_at = Timestamp("2026-09-13T13:00:00Z".into());
    repo.update_note(&touched).expect("touch first");
    let listed = repo.list_notes_by_folder(&f.id).expect("list");
    let titles: Vec<&str> = listed.iter().map(|n| n.title.as_str()).collect();
    assert_eq!(
        titles,
        vec!["first!", "third", "second"],
        "updated_at DESC order"
    );
}

fn folder_delete_restriction_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let repo = make();
    let has_notes = seed_folder(&repo, "f1", "Has notes");
    let empty = seed_folder(&repo, "f2", "Empty");
    let n = seed_note(&repo, "n1", &has_notes.id, "t", "", T0);

    assert_eq!(
        repo.delete_folder(&has_notes.id),
        Err(RepoError::FolderHasNotes),
        "folder with live notes"
    );

    // Even soft-deleted notes keep the folder non-deletable (until purged —
    // see `deleted_note_recovery_and_purge_lifecycle_case`).
    repo.soft_delete_note(&n.id).expect("soft delete");
    assert_eq!(
        repo.delete_folder(&has_notes.id),
        Err(RepoError::FolderHasNotes),
        "folder with soft-deleted notes"
    );

    // The empty folder deletes fine.
    repo.delete_folder(&empty.id).expect("delete empty folder");
}

fn error_paths_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let repo = make();
    let f = seed_folder(&repo, "f1", "Inbox");
    let n = seed_note(&repo, "n1", &f.id, "t", "", T0);

    // Note creation in a missing folder is rejected with NotFound.
    let orphan = Note::new(
        NoteDraft {
            folder_id: FolderId("ghost".into()),
            title: "t".into(),
            body: String::new(),
        },
        NoteId("n2".into()),
        Timestamp(T0.into()),
    )
    .expect("valid note");
    assert_eq!(repo.insert_note(&orphan), Err(RepoError::NotFound));

    // Duplicate id insert is a Conflict.
    let mut duplicate = n.clone();
    duplicate.title = "dupe".into();
    assert_eq!(repo.insert_note(&duplicate), Err(RepoError::Conflict));

    // A missing folder cannot be deleted twice.
    assert_eq!(
        repo.delete_folder(&FolderId("ghost".into())),
        Err(RepoError::NotFound)
    );

    // get_note on a missing id is Ok(None) (existence is not an error).
    assert_eq!(repo.get_note(&NoteId("ghost".into())).expect("get"), None);
}

/// Port-level adapter-parity contract (behavior thread 7 / inline thread
/// 3999753474): updating a note into a folder that does not exist is
/// `RepoError::NotFound` on EVERY adapter — never an opaque storage error.
fn update_note_to_missing_folder_is_not_found_case<R: NoteRepository + 'static>(
    make: &mut impl FnMut() -> R,
) {
    let repo = make();
    let f = seed_folder(&repo, "f1", "Inbox");
    let n = seed_note(&repo, "n1", &f.id, "wanderer", "body", T0);

    let mut moved = n.clone();
    moved.folder_id = FolderId("ghost-folder".into());
    let err = repo
        .update_note(&moved)
        .expect_err("update into a missing folder");
    assert!(
        matches!(err, RepoError::NotFound),
        "missing target folder must be RepoError::NotFound on every adapter, got {err:?}"
    );
}

fn folder_rename_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let repo = make();
    let f = seed_folder(&repo, "f1", "Before");
    let mut renamed = f.clone();
    renamed.name = "After".into();
    renamed.updated_at = Timestamp("2026-09-13T13:00:00Z".into());
    repo.update_folder(&renamed).expect("rename ok");
    assert_eq!(repo.get_folder(&f.id).expect("get").unwrap().name, "After");

    // Renaming a missing folder is NotFound.
    let mut ghost = renamed;
    ghost.id = FolderId("ghost".into());
    assert_eq!(repo.update_folder(&ghost), Err(RepoError::NotFound));
}
