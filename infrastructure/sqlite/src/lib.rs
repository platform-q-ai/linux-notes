//! SQLite adapter: migrations, repository, and FTS5 search over the application
//! ports. This is the only crate that talks to rusqlite; everything above it sees
//! the narrow `NoteRepository` / `SearchService` traits.

use std::path::Path;
use std::sync::Mutex;

use rusqlite::{Connection, OptionalExtension};
use rusty_notes_application::error::{RepoError, SearchError};
use rusty_notes_application::ports::{NoteRepository, SearchService};
use rusty_notes_domain::{Folder, FolderId, Note, NoteId, Timestamp};

/// Constructed when the backend lacks FTS5 (e.g. a non-bundled rusqlite build).
#[derive(Debug)]
pub struct FtsUnavailable(pub String);

impl std::fmt::Display for FtsUnavailable {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "FTS5 unavailable: {}", self.0)
    }
}

impl std::error::Error for FtsUnavailable {}

/// Current schema version. Bump and add a migration arm when the schema evolves.
pub const SCHEMA_VERSION: i64 = 1;

/// Error opening/migrating a database, including the FTS5 canary failure.
#[derive(Debug)]
pub enum OpenError {
    Rusqlite(rusqlite::Error),
    Fts(FtsUnavailable),
}

impl std::fmt::Display for OpenError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            OpenError::Rusqlite(e) => write!(f, "sqlite error: {e}"),
            OpenError::Fts(e) => write!(f, "{e}"),
        }
    }
}

impl std::error::Error for OpenError {}

impl From<rusqlite::Error> for OpenError {
    fn from(e: rusqlite::Error) -> Self {
        OpenError::Rusqlite(e)
    }
}

impl From<FtsUnavailable> for OpenError {
    fn from(e: FtsUnavailable) -> Self {
        OpenError::Fts(e)
    }
}

/// Opens (creating if needed) the database at `path` with the schema migrated to
/// the current version. The FTS5 canary runs here: a backend without FTS5 fails
/// fast with [`FtsUnavailable`] instead of failing later inside a search.
pub fn open(path: impl AsRef<Path>) -> Result<Connection, OpenError> {
    let conn = Connection::open(path)?;
    conn.pragma_update(None, "foreign_keys", "ON")?;
    migrate(&conn)?;
    fts5_canary(&conn)?;
    Ok(conn)
}

/// Opens an in-memory database (tests and throwaway evaluation).
pub fn open_in_memory() -> Result<Connection, OpenError> {
    let conn = Connection::open_in_memory()?;
    conn.pragma_update(None, "foreign_keys", "ON")?;
    migrate(&conn)?;
    fts5_canary(&conn)?;
    Ok(conn)
}

/// Applies pending migrations using the `user_version` pragma. Idempotent: calling
/// twice on a migrated database executes nothing and returns the same version.
pub fn migrate(conn: &Connection) -> Result<i64, rusqlite::Error> {
    let from = conn.query_row("PRAGMA user_version", [], |r| r.get(0))?;
    match from {
        0 => {
            conn.execute_batch(
                "BEGIN;
                 CREATE TABLE folders (
                     id TEXT PRIMARY KEY,
                     name TEXT NOT NULL CHECK (length(trim(name)) > 0),
                     created_at TEXT NOT NULL,
                     updated_at TEXT NOT NULL
                 );
                 CREATE TABLE notes (
                     id TEXT PRIMARY KEY,
                     folder_id TEXT NOT NULL REFERENCES folders(id),
                     title TEXT NOT NULL CHECK (length(trim(title)) > 0),
                     body TEXT NOT NULL DEFAULT '',
                     deleted_at TEXT,
                     created_at TEXT NOT NULL,
                     updated_at TEXT NOT NULL
                 );
                 CREATE INDEX idx_notes_folder ON notes(folder_id);
                 CREATE VIRTUAL TABLE notes_fts USING fts5(
                     title, body, content='notes', content_rowid='rowid'
                 );
                 CREATE TRIGGER notes_ai AFTER INSERT ON notes BEGIN
                     INSERT INTO notes_fts(rowid, title, body)
                         VALUES (new.rowid, new.title, new.body);
                 END;
                 CREATE TRIGGER notes_ad AFTER DELETE ON notes BEGIN
                     INSERT INTO notes_fts(notes_fts, rowid, title, body)
                         VALUES ('delete', old.rowid, old.title, old.body);
                 END;
                 CREATE TRIGGER notes_au AFTER UPDATE ON notes BEGIN
                     INSERT INTO notes_fts(notes_fts, rowid, title, body)
                         VALUES ('delete', old.rowid, old.title, old.body);
                     INSERT INTO notes_fts(rowid, title, body)
                         VALUES (new.rowid, new.title, new.body);
                 END;
                 COMMIT;",
            )?;
            conn.pragma_update(None, "user_version", SCHEMA_VERSION)?;
        }
        1 => {}
        v => {
            return Err(rusqlite::Error::InvalidParameterName(format!(
                "database schema version {v} is newer than this build supports ({SCHEMA_VERSION})"
            )));
        }
    }
    conn.query_row("PRAGMA user_version", [], |r| r.get(0))
}

/// Fails fast if FTS5 is not compiled into the sqlite build.
pub fn fts5_canary(conn: &Connection) -> Result<(), FtsUnavailable> {
    conn.execute("CREATE TEMP TABLE fts5_canary_probe(x)", [])
        .and_then(|_| conn.execute("DROP TABLE fts5_canary_probe", []))
        .map_err(|e| FtsUnavailable(e.to_string()))?;
    match conn.query_row("SELECT count(*) FROM notes_fts", [], |r| r.get::<_, i64>(0)) {
        Ok(_) => Ok(()),
        Err(e) => Err(FtsUnavailable(format!("notes_fts is not queryable: {e}"))),
    }
}

// ============================ repository ============================

fn map_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<Note> {
    Ok(Note {
        id: NoteId(row.get("id")?),
        folder_id: FolderId(row.get("folder_id")?),
        title: row.get("title")?,
        body: row.get("body")?,
        created_at: Timestamp(row.get("created_at")?),
        updated_at: Timestamp(row.get("updated_at")?),
    })
}

fn map_folder(row: &rusqlite::Row<'_>) -> rusqlite::Result<Folder> {
    Ok(Folder {
        id: FolderId(row.get("id")?),
        name: row.get("name")?,
        created_at: Timestamp(row.get("created_at")?),
        updated_at: Timestamp(row.get("updated_at")?),
    })
}

const NOTE_COLS: &str = "id, folder_id, title, body, created_at, updated_at";

/// SQLite-backed [`NoteRepository`]. Cheap to clone; all connections share the
/// mutex-protected pool. `foreign_keys=ON` is set on every connection (both here
/// and in [`open`]) so folder references are enforced at the database level too.
#[derive(Clone)]
pub struct SqliteNoteRepository {
    conn: std::sync::Arc<Mutex<Connection>>,
}

impl SqliteNoteRepository {
    pub fn new(conn: Connection) -> Self {
        let _ = conn.pragma_update(None, "foreign_keys", "ON");
        Self {
            conn: std::sync::Arc::new(Mutex::new(conn)),
        }
    }

    pub fn open(path: impl AsRef<Path>) -> Result<Self, OpenError> {
        Ok(Self::new(open(path)?))
    }

    pub fn open_in_memory() -> Result<Self, OpenError> {
        Ok(Self::new(open_in_memory()?))
    }

    /// Builds a repository over a shared connection handle (repo + search on one db).
    pub fn shared(conn: std::sync::Arc<Mutex<Connection>>) -> Self {
        Self { conn }
    }

    fn with_conn<T>(
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

fn map_rusqlite(e: rusqlite::Error) -> RepoError {
    match &e {
        rusqlite::Error::QueryReturnedNoRows => RepoError::NotFound,
        rusqlite::Error::SqliteFailure(err, msg)
            if err.code == rusqlite::ErrorCode::ConstraintViolation =>
        {
            match msg.as_deref().unwrap_or("").contains("UNIQUE") {
                true => RepoError::Conflict,
                false => RepoError::Storage(e.to_string()),
            }
        }
        _ => RepoError::Storage(e.to_string()),
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

    fn update_note(&self, note: &Note) -> Result<(), RepoError> {
        self.with_conn(|conn| {
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

/// Wall-clock timestamp for soft-delete stamps. SQLite-side default; use cases
/// normally supply timestamps via the Clock port, deletion here just needs a
/// monotone-enough marker.
fn now_stamp() -> String {
    // Fallback when the caller cannot inject a clock: seconds-precision UTC.
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| format!("{}-sym", d.as_secs()))
        .unwrap_or_else(|_| "deleted-sym".into())
}

// ============================ search ============================

/// FTS5-backed [`SearchService`]. Blank queries map to [`SearchError::EmptyQuery`];
/// user input is always passed as a parameter, and bare tokens are quoted for the
/// MATCH expression so quotes and hyphens are treated literally.
pub struct SqliteSearchService {
    conn: std::sync::Arc<Mutex<Connection>>,
}

impl SqliteSearchService {
    pub fn new(conn: Connection) -> Self {
        Self {
            conn: std::sync::Arc::new(Mutex::new(conn)),
        }
    }

    pub fn shared(conn: std::sync::Arc<Mutex<Connection>>) -> Self {
        Self { conn }
    }
}

/// Wraps each token in double quotes and ORs them, escaping embedded quotes by
/// doubling. `rusty "notes"` -> `"rusty" OR "\"notes\""`. Hyphenated tokens like
/// `todo-list` must NOT be turned into column filters or NOT operators.
/// Ranking weights (title=10, body=1) mirror the in-memory fake's contract:
/// title hits rank above body-only hits.
fn match_expression(query: &str) -> String {
    query
        .split_whitespace()
        .map(|token| format!("\"{}\"", token.replace('"', "\"\"")))
        .collect::<Vec<_>>()
        .join(" OR ")
}

impl SearchService for SqliteSearchService {
    fn search(&self, query: &str) -> Result<Vec<NoteId>, SearchError> {
        if query.trim().is_empty() {
            return Err(SearchError::EmptyQuery);
        }
        let expr = match_expression(query);
        let conn = self
            .conn
            .lock()
            .map_err(|_| SearchError::Storage("sqlite connection lock poisoned".into()))?;
        let mut stmt = conn
            .prepare(
                "SELECT n.id FROM notes_fts f
                 JOIN notes n ON n.rowid = f.rowid
                 WHERE notes_fts MATCH ?1 AND n.deleted_at IS NULL
                 ORDER BY bm25(notes_fts, 10.0, 1.0) ASC, n.updated_at DESC, n.id DESC
                 LIMIT 200",
            )
            .map_err(|e| SearchError::Storage(e.to_string()))?;
        let rows = stmt
            .query_map([&expr], |r| r.get::<_, String>(0))
            .map_err(|e| SearchError::Storage(e.to_string()))?
            .collect::<Result<Vec<_>, _>>()
            .map_err(|e| SearchError::Storage(e.to_string()))?;
        Ok(rows.into_iter().map(NoteId).collect())
    }
}
