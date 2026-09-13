//! The composition root: builds the real production adapters and wires them into
//! the use-case bundle the shell executes.
//!
//! Thread 2 / F8: production identity comes from [`SystemClock`] and
//! [`UniqueIds`] (adapters/system) — never from test-support fakes. [`UniqueIds`]
//! seeds itself from the database it is pointed at, so ids stay unique across app
//! restarts; `SequentialIds` (a per-process counter in test-support) would
//! collide after every restart and is forbidden here.

use std::sync::Arc;

use rusty_notes_application::ports::{Clock, Ids, NoteRepository, SearchService};
use rusty_notes_application::use_cases::folders::{
    CreateFolder, DeleteFolder, ListFolders, RenameFolder,
};
use rusty_notes_application::use_cases::notes::{
    CreateNote, DeleteNote, ListNotes, MoveNote, OpenNote, SearchNotes, UpdateNote,
};
use rusty_notes_sqlite::{SqliteNoteRepository, SqliteSearchService};
use rusty_notes_system::{SystemClock, UniqueIds};

use crate::app::Actions;

/// Database location: `rusty-notes.db` in the current directory (local-first).
pub const DB_FILE: &str = "rusty-notes.db";

/// Builds the production wiring: ONE SQLite connection shared by repository and
/// search, wrapped into the use cases via `Arc<dyn Port>`, with the production
/// system clock and self-seeding unique-id adapter.
pub fn build_wired(db_path: &str) -> Result<Actions, String> {
    let conn = rusty_notes_sqlite::open(db_path).map_err(|e| e.to_string())?;
    let conn = Arc::new(std::sync::Mutex::new(conn));

    let repo: Arc<dyn NoteRepository> = Arc::new(SqliteNoteRepository::shared(conn.clone()));
    let search: Arc<dyn SearchService> = Arc::new(SqliteSearchService::shared(conn));
    let clock: Arc<dyn Clock> = Arc::new(SystemClock);
    let ids: Arc<dyn Ids> = Arc::new(UniqueIds::default());

    Ok(Actions {
        list_notes: ListNotes::new(repo.clone()),
        list_folders: ListFolders::new(repo.clone()),
        open_note: OpenNote::new(repo.clone()),
        create_note: CreateNote::new(repo.clone(), ids.clone(), clock.clone()),
        update_note: UpdateNote::new(repo.clone(), clock.clone()),
        delete_note: DeleteNote::new(repo.clone()),
        move_note: MoveNote::new(repo.clone(), clock.clone()),
        create_folder: CreateFolder::new(repo.clone(), ids.clone(), clock.clone()),
        rename_folder: RenameFolder::new(repo.clone(), clock.clone()),
        delete_folder: DeleteFolder::new(repo.clone()),
        search: SearchNotes::new(search, repo),
    })
}
