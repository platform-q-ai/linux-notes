//! Shared behavioral contract for every [`NoteRepository`] / [`SearchService`]
//! implementation.
//!
//! Each adapter adds one tiny test target that invokes [`run_all`] /
//! [`run_all_search`] with its own factories. That proves substitutability (LSP)
//! with behavioral tests instead of inheritance: the in-memory fake and the real
//! SQLite adapter must pass the very same cases.
//!
//! Fresh state per case: every case builds its own repository via the factory, so
//! cases never share mutable fixtures. "Reopen" (close and re-open the same
//! backend) is adapter-specific — the SQLite adapter proves it in its integration
//! tests by re-opening the same temp-dir database file; the in-memory fake proves
//! shared-store behavior via its `Clone`.

use std::sync::Arc;

use rusty_notes_domain::{FolderId, Note, NoteDraft, NoteId, Timestamp};

use crate::error::RepoError;
use crate::memory::{FixedClock, SequentialIds};
use crate::ports::{NoteRepository, SearchService};
use crate::use_cases::{
    CreateFolder, CreateNote, DeleteFolder, DeleteNote, ListNotes, MoveNote, RenameFolder,
    UpdateNote,
};

const T0: &str = "2026-09-13T12:00:00Z";
const T1: &str = "2026-09-13T13:00:00Z";

/// Runs the full repository contract against a factory of fresh repositories.
/// `make` must return an independent repository over independent state per call.
pub fn run_all<R: NoteRepository + 'static>(make: impl FnMut() -> R) {
    let mut make = make;
    round_trip_case(&mut make);
    update_overwrite_case(&mut make);
    delete_semantics_case(&mut make);
    folder_scoped_listing_case(&mut make);
    ordering_case(&mut make);
    folder_delete_restriction_case(&mut make);
    error_paths_case(&mut make);
    folder_rename_case(&mut make);
}

/// Search-service contract. The factory returns a repo and a search service
/// wired to the *same* underlying store, plus it must hand the repo back so the
/// harness can populate data through the port.
pub fn run_all_search(make: impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>)) {
    let mut make = make;
    search_ranks_case(&mut make);
    search_blank_query_case(&mut make);
    search_excludes_deleted_case(&mut make);
}

struct Fixture {
    clock: Arc<FixedClock>,
    create_note: CreateNote,
    update_note: UpdateNote,
    move_note: MoveNote,
    delete_note: DeleteNote,
    list_notes: ListNotes,
    create_folder: CreateFolder,
    rename_folder: RenameFolder,
    delete_folder: DeleteFolder,
}

fn fixture<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) -> Fixture {
    let repo: Arc<dyn NoteRepository> = Arc::new(make());
    let ids = Arc::new(SequentialIds::default());
    let clock = Arc::new(FixedClock::new(Timestamp(T0.into())));
    let create_note = CreateNote::new(repo.clone(), ids.clone(), clock.clone());
    let update_note = UpdateNote::new(repo.clone(), clock.clone());
    let move_note = MoveNote::new(repo.clone(), clock.clone());
    let delete_note = DeleteNote::new(repo.clone());
    let list_notes = ListNotes::new(repo.clone());
    let create_folder = CreateFolder::new(repo.clone(), ids.clone(), clock.clone());
    let rename_folder = RenameFolder::new(repo.clone(), clock.clone());
    let delete_folder = DeleteFolder::new(repo.clone());
    Fixture {
        clock,
        create_note,
        update_note,
        move_note,
        delete_note,
        list_notes,
        create_folder,
        rename_folder,
        delete_folder,
    }
}

impl Fixture {
    fn folder(&self, name: &str) -> rusty_notes_domain::Folder {
        self.create_folder
            .execute(name.to_string())
            .expect("create folder")
    }

    fn note(&self, folder_id: &FolderId, title: &str, body: &str) -> Note {
        self.create_note
            .execute(NoteDraft {
                folder_id: folder_id.clone(),
                title: title.into(),
                body: body.into(),
            })
            .expect("create note")
    }
}

fn round_trip_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let fx = fixture(make);
    let f = fx.folder("Inbox");
    let n = fx.note(&f.id, "title one", "body one");
    let listed = fx.list_notes.execute(Some(f.id.clone())).expect("list");
    assert_eq!(listed.len(), 1);
    assert_eq!(listed[0].id, n.id);
    assert_eq!(listed[0].title, "title one");
    assert_eq!(listed[0].body, "body one");
    assert_eq!(listed[0].created_at, listed[0].updated_at);
}

fn update_overwrite_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let fx = fixture(make);
    let f = fx.folder("Inbox");
    let n = fx.note(&f.id, "before", "old body");
    fx.clock.advance(Timestamp(T1.into())); // updated_at must differ from created_at
    let updated = fx
        .update_note
        .execute(&n.id, Some("after".into()), Some("new body".into()))
        .expect("update");
    assert_eq!(updated.title, "after");
    assert_eq!(updated.body, "new body");
    assert_eq!(updated.updated_at, Timestamp(T1.into()));

    // `None` title keeps the stored title (partial update).
    let kept = fx
        .update_note
        .execute(&n.id, None, Some("newer body".into()))
        .expect("update 2");
    assert_eq!(kept.title, "after");
    assert_eq!(kept.body, "newer body");

    // Unknown note cannot be updated.
    let err = fx
        .update_note
        .execute(&NoteId("missing".into()), Some("x".into()), None)
        .expect_err("missing note");
    assert!(matches!(err, crate::error::AppError::NoteMissing));
}

fn delete_semantics_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let fx = fixture(make);
    let f = fx.folder("Inbox");
    let n = fx.note(&f.id, "doomed", "body");
    fx.delete_note.execute(&n.id).expect("delete ok");

    // Deleted notes vanish from listings and cannot be updated.
    assert!(fx
        .list_notes
        .execute(Some(f.id.clone()))
        .expect("list")
        .is_empty());
    assert!(fx.list_notes.execute(None).expect("list all").is_empty());
    let err = fx
        .update_note
        .execute(&n.id, Some("zombie".into()), None)
        .expect_err("deleted note must not update");
    assert!(matches!(err, crate::error::AppError::NoteMissing));

    // Deleting again is NotFound: deletion is explicit, the caller learns it
    // deleted nothing.
    let err = fx.delete_note.execute(&n.id).expect_err("second delete");
    assert!(matches!(
        err,
        crate::error::AppError::Repo(RepoError::NotFound)
    ));
}

fn folder_scoped_listing_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let fx = fixture(make);
    let f1 = fx.folder("One");
    let f2 = fx.folder("Two");
    let n1 = fx.note(&f1.id, "in one", "");
    let _n2 = fx.note(&f2.id, "in two", "");
    let in_one = fx.list_notes.execute(Some(f1.id.clone())).expect("list f1");
    assert_eq!(in_one.len(), 1);
    assert_eq!(in_one[0].id, n1.id);
    assert_eq!(fx.list_notes.execute(None).expect("list all").len(), 2);
}

fn ordering_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let fx = fixture(make);
    let f = fx.folder("Inbox");
    let first = fx.note(&f.id, "first", "");
    let _second = fx.note(&f.id, "second", "");
    let _third = fx.note(&f.id, "third", "");
    // Touch `first` so it becomes the most recently updated.
    fx.clock.advance(Timestamp(T1.into()));
    fx.update_note
        .execute(&first.id, Some("first!".into()), None)
        .expect("touch first");
    let listed = fx.list_notes.execute(Some(f.id.clone())).expect("list");
    let titles: Vec<&str> = listed.iter().map(|n| n.title.as_str()).collect();
    assert_eq!(
        titles,
        vec!["first!", "third", "second"],
        "updated_at DESC order"
    );
}

fn folder_delete_restriction_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let fx = fixture(make);
    let has_notes = fx.folder("Has notes");
    let empty = fx.folder("Empty");
    let n = fx.note(&has_notes.id, "t", "");

    let err = fx
        .delete_folder
        .execute(&has_notes.id)
        .expect_err("folder with notes");
    assert!(matches!(
        err,
        crate::error::AppError::Repo(RepoError::FolderHasNotes)
    ));

    // Even soft-deleted notes keep the folder non-deletable.
    fx.delete_note.execute(&n.id).expect("soft delete");
    let err = fx
        .delete_folder
        .execute(&has_notes.id)
        .expect_err("folder with soft-deleted notes");
    assert!(matches!(
        err,
        crate::error::AppError::Repo(RepoError::FolderHasNotes)
    ));

    // The empty folder deletes fine.
    fx.delete_folder
        .execute(&empty.id)
        .expect("delete empty folder");
}

fn error_paths_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let fx = fixture(make);
    let f = fx.folder("Inbox");
    let n = fx.note(&f.id, "t", "");

    // Note creation in a missing folder is rejected.
    let err = fx
        .create_note
        .execute(NoteDraft {
            folder_id: FolderId("ghost".into()),
            title: "t".into(),
            body: String::new(),
        })
        .expect_err("missing folder");
    assert!(matches!(err, crate::error::AppError::FolderMissing));

    // Move to a missing folder is rejected.
    let err = fx
        .move_note
        .execute(&n.id, FolderId("ghost".into()))
        .expect_err("missing target");
    assert!(matches!(err, crate::error::AppError::FolderMissing));

    // Empty title rejected by the domain through the use case.
    let err = fx
        .create_note
        .execute(NoteDraft {
            folder_id: f.id.clone(),
            title: "   ".into(),
            body: String::new(),
        })
        .expect_err("empty title");
    assert!(matches!(err, crate::error::AppError::Domain(_)));
}

fn folder_rename_case<R: NoteRepository + 'static>(make: &mut impl FnMut() -> R) {
    let fx = fixture(make);
    let f = fx.folder("Before");
    fx.clock.advance(Timestamp(T1.into()));
    let renamed = fx
        .rename_folder
        .execute(&f.id, "After".into())
        .expect("rename ok");
    assert_eq!(renamed.name, "After");
    assert_eq!(renamed.updated_at, Timestamp(T1.into()));

    // Renaming to a blank name is a domain error, not a storage error.
    let err = fx
        .rename_folder
        .execute(&f.id, "   ".into())
        .expect_err("blank name");
    assert!(matches!(err, crate::error::AppError::Domain(_)));

    // Renaming a missing folder is FolderMissing.
    let err = fx
        .rename_folder
        .execute(&FolderId("ghost".into()), "x".into())
        .expect_err("missing folder");
    assert!(matches!(err, crate::error::AppError::FolderMissing));
}

fn search_ranks_case(make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>)) {
    let (repo, search) = make();
    let ids = Arc::new(SequentialIds::default());
    let clock = Arc::new(FixedClock::new(Timestamp(T0.into())));
    let create_folder = CreateFolder::new(repo.clone(), ids.clone(), clock.clone());
    let create_note = CreateNote::new(repo.clone(), ids, clock);
    let f = create_folder.execute("Inbox".into()).expect("folder");
    let _title_hit = create_note
        .execute(NoteDraft {
            folder_id: f.id.clone(),
            title: "budget report".into(),
            body: "nothing relevant".into(),
        })
        .expect("note 1");
    let _body_hit = create_note
        .execute(NoteDraft {
            folder_id: f.id.clone(),
            title: "random".into(),
            body: "budget line".into(),
        })
        .expect("note 2");

    let hits = search.search("budget").expect("search ok");
    assert_eq!(hits.len(), 2, "both notes mention budget");
    // Title hits rank above body-only hits.
    assert_eq!(hits[0], _title_hit.id);
}

fn search_blank_query_case(
    make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>),
) {
    let (_repo, search) = make();
    let err = search.search("   ").expect_err("blank query");
    assert!(matches!(err, crate::error::SearchError::EmptyQuery));
    let err = search.search("").expect_err("empty query");
    assert!(matches!(err, crate::error::SearchError::EmptyQuery));
}

fn search_excludes_deleted_case(
    make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>),
) {
    let (repo, search) = make();
    let ids = Arc::new(SequentialIds::default());
    let clock = Arc::new(FixedClock::new(Timestamp(T0.into())));
    let create_folder = CreateFolder::new(repo.clone(), ids.clone(), clock.clone());
    let create_note = CreateNote::new(repo.clone(), ids, clock);
    let delete_note = DeleteNote::new(repo.clone());
    let f = create_folder.execute("Inbox".into()).expect("folder");
    let n = create_note
        .execute(NoteDraft {
            folder_id: f.id.clone(),
            title: "unique-needle".into(),
            body: String::new(),
        })
        .expect("note");
    assert_eq!(
        search.search("unique-needle").expect("hits"),
        vec![n.id.clone()]
    );
    delete_note.execute(&n.id).expect("soft delete");
    assert!(
        search
            .search("unique-needle")
            .expect("search after delete")
            .is_empty(),
        "soft-deleted notes must not appear in search"
    );
}
