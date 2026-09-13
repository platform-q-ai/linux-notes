//! Versioned schema migrations using the `user_version` pragma.

use rusqlite::Connection;

/// Current schema version. Bump and add a migration arm when the schema evolves.
pub const SCHEMA_VERSION: i64 = 1;

/// Applies pending migrations. Idempotent: calling twice on a migrated database
/// executes nothing and returns the same version.
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
