//! Integration tests: trigger-synced FTS, migration idempotency, reopen
//! persistence, folder-delete restriction, MATCH escaping. Each test owns a fresh
//! temp-dir database.

use std::collections::HashMap;

use rusty_notes_application::contract::{run_all, run_all_search};
use rusty_notes_application::error::RepoError;
use rusty_notes_application::ports::{NoteRepository, SearchService};
use rusty_notes_domain::{Folder, FolderId, Note, NoteDraft, NoteId, Timestamp};
use rusty_notes_sqlite::{SqliteNoteRepository, SqliteSearchService};

fn temp_repo() -> (SqliteNoteRepository, tempfile::TempDir) {
    let dir = tempfile::tempdir().expect("tempdir");
    let repo = SqliteNoteRepository::open(dir.path().join("notes.db")).expect("open db");
    (repo, dir)
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

/// The shared contract suite against the real backend.
#[test]
fn sqlite_repository_honors_the_contract() {
    run_all(|| SqliteNoteRepository::open_in_memory().expect("in-memory db"));
}

/// The shared search contract against the real backend (same database as the repo).
#[test]
fn sqlite_search_honors_the_search_contract() {
    run_all_search(|| {
        let conn = rusty_notes_sqlite::open_in_memory().expect("db");
        let conn = std::sync::Arc::new(std::sync::Mutex::new(conn));
        let repo: std::sync::Arc<dyn NoteRepository> =
            std::sync::Arc::new(SqliteNoteRepository::shared(conn.clone()));
        let search: std::sync::Arc<dyn SearchService> =
            std::sync::Arc::new(SqliteSearchService::shared(conn));
        (repo, search)
    });
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

#[test]
fn fts_stays_synced_through_insert_update_delete() {
    let (_repo, _dir) = temp_repo();
    let conn = std::sync::Arc::new(std::sync::Mutex::new(
        rusty_notes_sqlite::open_in_memory().expect("db"),
    ));
    let repo = SqliteNoteRepository::shared(conn.clone());
    let search = SqliteSearchService::shared(conn);

    let f = folder(&repo, "f1", "Inbox");
    let n = note(&repo, &f, "n1", "quarterly budget", "numbers inside");

    let hits = search.search("budget").expect("search 1");
    assert_eq!(hits, vec![NoteId("n1".into())]);

    // UPDATE keeps the index in sync (trigger notes_au). The title still says
    // "budget", so that term keeps matching; the old body term must leave.
    let mut updated = n.clone();
    updated.edit_body(
        "nothing relevant now".into(),
        Timestamp("2026-09-13T13:00:00Z".into()),
    );
    repo.update_note(&updated).expect("update");
    assert!(
        search.search("inside").expect("search 2").is_empty(),
        "old body content must leave the index after update"
    );
    assert_eq!(search.search("relevant").expect("search 3").len(), 1);
    assert_eq!(
        search.search("budget").expect("title still matches").len(),
        1
    );

    // A full update (title included) also leaves via the same trigger.
    let mut renamed = updated;
    renamed.title = "placeholder".into();
    repo.update_note(&renamed).expect("update 2");
    assert!(search.search("budget").expect("search 4").is_empty());

    // Soft-deleted rows drop out of search (deleted_at filter).
    repo.soft_delete_note(&renamed.id).expect("soft delete");
    assert!(search.search("relevant").expect("search 5").is_empty());
}

#[test]
fn soft_delete_hides_note_from_listings_and_updates() {
    let (repo, _dir) = temp_repo();
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
    let (repo, _dir) = temp_repo();
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

#[test]
fn match_escaping_quotes_and_hyphens() {
    let (_repo, _dir) = temp_repo();
    let conn = std::sync::Arc::new(std::sync::Mutex::new(
        rusty_notes_sqlite::open_in_memory().expect("db"),
    ));
    let repo = SqliteNoteRepository::shared(conn.clone());
    let search = SqliteSearchService::shared(conn);

    let f = folder(&repo, "f1", "Inbox");
    note(&repo, &f, "n1", r#"the "quoted" title"#, "a todo-list item");
    note(&repo, &f, "n2", "plain title", "no marker here");

    // Hyphen inside a token must not be parsed as NOT / column filter.
    let hits = search.search("todo-list").expect("hyphen search");
    assert_eq!(hits.len(), 1);
    assert_eq!(hits[0], NoteId("n1".into()));

    // Embedded double quote must not break the MATCH expression.
    let hits = search.search(r#""quoted""#).expect("quote search");
    assert_eq!(hits.len(), 1);

    // Blank queries are rejected by contract.
    assert!(search.search("   ").is_err());
    let _ = &repo;
    let _ = HashMap::<String, usize>::new();
}

#[test]
fn probe_fts_state_after_update() {
    let conn = rusty_notes_sqlite::open_in_memory().expect("db");
    conn.execute(
        "INSERT INTO folders (id,name,created_at,updated_at) VALUES ('f1','Inbox','t','t')",
        [],
    )
    .unwrap();
    conn.execute("INSERT INTO notes (id,folder_id,title,body,created_at,updated_at) VALUES ('n1','f1','t','budget numbers','t','t')", []).unwrap();
    let c: i64 = conn
        .query_row(
            "SELECT count(*) FROM notes_fts WHERE notes_fts MATCH 'budget'",
            [],
            |r| r.get(0),
        )
        .unwrap();
    println!("before update: {c}");
    conn.execute(
        "UPDATE notes SET body='nothing relevant now', updated_at='t2' WHERE id='n1'",
        [],
    )
    .unwrap();
    let c: i64 = conn
        .query_row(
            "SELECT count(*) FROM notes_fts WHERE notes_fts MATCH 'budget'",
            [],
            |r| r.get(0),
        )
        .unwrap();
    println!("after update: {c}");
    let mut stmt = conn
        .prepare("SELECT rowid, title, body FROM notes_fts")
        .unwrap();
    let rows: Vec<(i64, String, String)> = stmt
        .query_map([], |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)))
        .unwrap()
        .map(|x| x.unwrap())
        .collect();
    println!("fts rows: {rows:?}");
}
