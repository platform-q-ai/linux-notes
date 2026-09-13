//! Physical connection handling and the FTS5 availability canary.

use std::path::Path;

use rusqlite::Connection;

use crate::migrations::migrate;

/// Constructed when the backend lacks FTS5 (e.g. a non-bundled rusqlite build).
#[derive(Debug)]
pub struct FtsUnavailable(pub String);

impl std::fmt::Display for FtsUnavailable {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "FTS5 unavailable: {}", self.0)
    }
}

impl std::error::Error for FtsUnavailable {}

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
