//! SQLite-backed [`NoteRepository`].
//!
//! Ordering, soft-delete and folder-protection semantics are pinned by the
//! shared contract suite (`rusty_notes_test_support::contracts::run_all`), which
//! this adapter runs in `tests/repository_contract.rs`.

use std::path::Path;
use std::sync::{Arc, Mutex};

use rusqlite::{Connection, OptionalExtension};
use rusty_notes_application::error::RepoError;
use rusty_notes_application::ports::NoteRepository;
use rusty_notes_domain::{Folder, FolderId, Note, NoteId, Timestamp};

use crate::connection::{open, open_in_memory, OpenError};
use crate::error_mapping::{map_rusqlite, now_stamp};

pub(crate) fn map_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<Note> {
    Ok(Note {
        id: NoteId(row.get("id")?),
        folder_id: FolderId(row.get("folder_id")?),
        title: row.get("title")?,
        body: row.get("body")?,
        created_at: Timestamp(row.get("created_at")?),
        updated_at: Timestamp(row.get("updated_at")?),
    })
}

pub(crate) fn map_folder(row: &rusqlite::Row<'_>) -> rusqlite::Result<Folder> {
    Ok(Folder {
        id: FolderId(row.get("id")?),
        name: row.get("name")?,
        created_at: Timestamp(row.get("created_at")?),
        updated_at: Timestamp(row.get("updated_at")?),
    })
}

pub(crate) const NOTE_COLS: &str = "id, folder_id, title, body, created_at, updated_at";

/// SQLite-backed [`NoteRepository`]. Cheap to clone; all connections share the
/// mutex-protected pool. `foreign_keys=ON` is set on every connection (both here
/// and in [`open`] via `connection::open`) so folder references are enforced at
/// the database level too.
#[derive(Clone)]
pub struct SqliteNoteRepository {
    conn: Arc<Mutex<Connection>>,
}

impl SqliteNoteRepository {
    pub fn new(conn: Connection) -> Self {
        let _ = conn.pragma_update(None, "foreign_keys", "ON");
        Self {
            conn: Arc::new(Mutex::new(conn)),
        }
    }

    pub fn open(path: impl AsRef<Path>) -> Result<Self, OpenError> {
        Ok(Self::new(open(path)?))
    }

    pub fn open_in_memory() -> Result<Self, OpenError> {
        Ok(Self::new(open_in_memory()?))
    }

    /// Builds a repository over a shared connection handle (repo + search on one db).
    pub fn shared(conn: Arc<Mutex<Connection>>) -> Self {
        Self { conn }
    }

    pub(crate) fn with_conn<T>(
        &self,
        f: impl FnOnce(&Connection) -> Result<T, RepoError>,
    ) -> Result<T, RepoError> {
        let conn = self
            .conn
            .lock()
            .map_err(|_| RepoError::Storage("sqlite connection lock poisoned".into()))?;
        f(&conn)
    }
}

impl NoteRepository for SqliteNoteRepository {
    fn insert_note(&self, note: &Note) -> Result<(), RepoError> {
        self.with_conn(|conn| {
            // Folder existence first: a clean NotFound instead of a raw FK error.
            let exists: i64 = conn
                .query_row(
                    "SELECT count(*) FROM folders WHERE id = ?1",
                    [&note.folder_id.0],
                    |r| r.get(0),
                )
                .map_err(map_rusqlite)?;
            if exists == 0 {
                return Err(RepoError::NotFound);
            }
            conn.execute(
                "INSERT INTO notes (id, folder_id, title, body, created_at, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
                rusqlite::params![
                    note.id.0,
                    note.folder_id.0,
                    note.title,
                    note.body,
                    note.created_at.0,
                    note.updated_at.0
                ],
            )
            .map_err(map_rusqlite)?;
            Ok(())
        })
    }

    /// Plain reads exclude soft-deleted rows (thread 6 truth).
    fn get_note(&self, id: &NoteId) -> Result<Option<Note>, RepoError> {
        self.with_conn(|conn| {
            let row = conn
                .query_row(
                    &format!("SELECT {NOTE_COLS} FROM notes WHERE id = ?1 AND deleted_at IS NULL"),
                    [&id.0],
                    map_row,
                )
                .optional()
                .map_err(map_rusqlite)?;
            Ok(row)
        })
    }

    /// Explicit recovery path: returns the row even when soft-deleted. `None`
    /// only when the note was never stored or has been purged.
    fn get_note_including_deleted(&self, id: &NoteId) -> Result<Option<Note>, RepoError> {
        self.with_conn(|conn| {
            let row = conn
                .query_row(
                    &format!("SELECT {NOTE_COLS} FROM notes WHERE id = ?1"),
                    [&id.0],
                    map_row,
                )
                .optional()
                .map_err(map_rusqlite)?;
            Ok(row)
        })
    }

    /// Full overwrite of a live note. The target folder is pre-checked so a
    /// missing folder is a clean `RepoError::NotFound` (thread 7 parity with the
    /// in-memory fake), never an opaque storage error.
    fn update_note(&self, note: &Note) -> Result<(), RepoError> {
        self.with_conn(|conn| {
            let folder_exists: i64 = conn
                .query_row(
                    "SELECT count(*) FROM folders WHERE id = ?1",
                    [&note.folder_id.0],
                    |r| r.get(0),
                )
                .map_err(map_rusqlite)?;
            if folder_exists == 0 {
                return Err(RepoError::NotFound);
            }
            let n = conn
                .execute(
                    "UPDATE notes SET folder_id = ?2, title = ?3, body = ?4, updated_at = ?5
                     WHERE id = ?1 AND deleted_at IS NULL",
                    rusqlite::params![
                        note.id.0,
                        note.folder_id.0,
                        note.title,
                        note.body,
                        note.updated_at.0
                    ],
                )
                .map_err(map_rusqlite)?;
            if n == 0 {
                return Err(RepoError::NotFound);
            }
            Ok(())
        })
    }

    /// Soft delete. Missing notes are `RepoError::NotFound` (deleting is explicit,
    /// so callers learn they deleted nothing).
    fn soft_delete_note(&self, id: &NoteId) -> Result<(), RepoError> {
        self.with_conn(|conn| {
            let n = conn
                .execute(
                    "UPDATE notes SET deleted_at = ?2 WHERE id = ?1 AND deleted_at IS NULL",
                    rusqlite::params![id.0, now_stamp()],
                )
                .map_err(map_rusqlite)?;
            if n == 0 {
                return Err(RepoError::NotFound);
            }
            Ok(())
        })
    }

    /// Permanently removes a soft-deleted note ("empty trash"). Live or missing
    /// notes are `RepoError::NotFound`. The row leaves the whole port surface and
    /// stops blocking its folder from being deleted (thread 6 lifecycle).
    fn purge_note(&self, id: &NoteId) -> Result<(), RepoError> {
        self.with_conn(|conn| {
            let is_deleted: Option<String> = conn
                .query_row(
                    "SELECT id FROM notes WHERE id = ?1 AND deleted_at IS NOT NULL",
                    [&id.0],
                    |r| r.get(0),
                )
                .optional()
                .map_err(map_rusqlite)?;
            if is_deleted.is_none() {
                return Err(RepoError::NotFound);
            }
            conn.execute("DELETE FROM notes WHERE id = ?1", [&id.0])
                .map_err(map_rusqlite)?;
            Ok(())
        })
    }

    fn list_notes_by_folder(&self, folder_id: &FolderId) -> Result<Vec<Note>, RepoError> {
        self.with_conn(|conn| {
            let mut stmt = conn
                .prepare(&format!(
                    "SELECT {NOTE_COLS} FROM notes
                     WHERE folder_id = ?1 AND deleted_at IS NULL
                     ORDER BY updated_at DESC, id DESC"
                ))
                .map_err(map_rusqlite)?;
            let rows = stmt
                .query_map([&folder_id.0], map_row)
                .map_err(map_rusqlite)?
                .collect::<Result<Vec<_>, _>>()
                .map_err(map_rusqlite)?;
            Ok(rows)
        })
    }

    fn list_all_notes(&self) -> Result<Vec<Note>, RepoError> {
        self.with_conn(|conn| {
            let mut stmt = conn
                .prepare(&format!(
                    "SELECT {NOTE_COLS} FROM notes WHERE deleted_at IS NULL
                     ORDER BY updated_at DESC, id DESC"
                ))
                .map_err(map_rusqlite)?;
            let rows = stmt
                .query_map([], map_row)
                .map_err(map_rusqlite)?
                .collect::<Result<Vec<_>, _>>()
                .map_err(map_rusqlite)?;
            Ok(rows)
        })
    }

    fn insert_folder(&self, folder: &Folder) -> Result<(), RepoError> {
        self.with_conn(|conn| {
            conn.execute(
                "INSERT INTO folders (id, name, created_at, updated_at) VALUES (?1, ?2, ?3, ?4)",
                rusqlite::params![
                    folder.id.0,
                    folder.name,
                    folder.created_at.0,
                    folder.updated_at.0
                ],
            )
            .map_err(map_rusqlite)?;
            Ok(())
        })
    }

    fn get_folder(&self, id: &FolderId) -> Result<Option<Folder>, RepoError> {
        self.with_conn(|conn| {
            let row = conn
                .query_row(
                    "SELECT id, name, created_at, updated_at FROM folders WHERE id = ?1",
                    [&id.0],
                    map_folder,
                )
                .optional()
                .map_err(map_rusqlite)?;
            Ok(row)
        })
    }

    fn list_folders(&self) -> Result<Vec<Folder>, RepoError> {
        self.with_conn(|conn| {
            let mut stmt = conn
                .prepare("SELECT id, name, created_at, updated_at FROM folders ORDER BY name ASC, id ASC")
                .map_err(map_rusqlite)?;
            let rows = stmt
                .query_map([], map_folder)
                .map_err(map_rusqlite)?
                .collect::<Result<Vec<_>, _>>()
                .map_err(map_rusqlite)?;
            Ok(rows)
        })
    }

    fn update_folder(&self, folder: &Folder) -> Result<(), RepoError> {
        self.with_conn(|conn| {
            let n = conn
                .execute(
                    "UPDATE folders SET name = ?2, updated_at = ?3 WHERE id = ?1",
                    rusqlite::params![folder.id.0, folder.name, folder.updated_at.0],
                )
                .map_err(map_rusqlite)?;
            if n == 0 {
                return Err(RepoError::NotFound);
            }
            Ok(())
        })
    }

    /// Refuses with `RepoError::FolderHasNotes` while ANY note — live or
    /// soft-deleted — references the folder; purge the deleted notes to clear
    /// the folder (thread 6).
    fn delete_folder(&self, id: &FolderId) -> Result<(), RepoError> {
        self.with_conn(|conn| {
            let referenced: i64 = conn
                .query_row(
                    "SELECT count(*) FROM notes WHERE folder_id = ?1",
                    [&id.0],
                    |r| r.get(0),
                )
                .map_err(map_rusqlite)?;
            if referenced > 0 {
                return Err(RepoError::FolderHasNotes);
            }
            let n = conn
                .execute("DELETE FROM folders WHERE id = ?1", [&id.0])
                .map_err(map_rusqlite)?;
            if n == 0 {
                return Err(RepoError::NotFound);
            }
            Ok(())
        })
    }
}
