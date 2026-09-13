//! Search contract suite for the SQLite adapter (dev-dependency:
//! `rusty-notes-test-support`). The FTS5 backend must pass the exact same
//! parity contract as the in-memory fake (behavior thread 3): whole-token
//! matching, OR across query groups, title-above-body ranking, 200-result cap,
//! blank-query rejection, soft-deleted exclusion — plus MATCH-expression
//! escaping pinned here.

use std::sync::{Arc, Mutex};

use rusty_notes_application::ports::{NoteRepository, SearchService};
use rusty_notes_sqlite::{SqliteNoteRepository, SqliteSearchService};
use rusty_notes_test_support::contracts::run_all_search;

/// The shared search contract against the real backend (same database as the repo).
#[test]
fn sqlite_search_honors_the_search_contract() {
    run_all_search(|| {
        let conn = rusty_notes_sqlite::open_in_memory().expect("db");
        let conn = Arc::new(Mutex::new(conn));
        let repo: Arc<dyn NoteRepository> = Arc::new(SqliteNoteRepository::shared(conn.clone()));
        let search: Arc<dyn SearchService> = Arc::new(SqliteSearchService::shared(conn));
        (repo, search)
    });
}

// ---------------- SQLite-specific search assertions ----------------

use rusty_notes_domain::{Folder, FolderId, Note, NoteDraft, NoteId, Timestamp};

fn shared_pair() -> (SqliteNoteRepository, SqliteSearchService) {
    let conn = rusty_notes_sqlite::open_in_memory().expect("db");
    let conn = Arc::new(Mutex::new(conn));
    let repo = SqliteNoteRepository::shared(conn.clone());
    let search = SqliteSearchService::shared(conn);
    (repo, search)
}

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
fn match_escaping_quotes_and_hyphens() {
    let (repo, search) = shared_pair();
    let f = folder(&repo, "f1", "Inbox");
    note(&repo, &f, "n1", r#"the "quoted" title"#, "a todo-list item");
    note(&repo, &f, "n2", "plain title", "no marker here");

    // Hyphen inside a token must not be parsed as NOT / column filter: the group
    // is quoted into a phrase of adjacent tokens.
    let hits = search.search("todo-list").expect("hyphen search");
    assert_eq!(hits.len(), 1);
    assert_eq!(hits[0], NoteId("n1".into()));

    // Embedded double quote must not break the MATCH expression.
    let hits = search.search(r#""quoted""#).expect("quote search");
    assert_eq!(hits.len(), 1);

    // Blank queries are rejected by contract.
    assert!(search.search("   ").is_err());
}
