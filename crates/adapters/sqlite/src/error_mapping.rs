//! rusqlite error mapping to the application port errors, plus the wall-clock
//! stamp used for soft-delete markers.

use rusty_notes_application::error::RepoError;

/// Maps raw rusqlite errors onto the port error surface. Constraint violations
/// with a UNIQUE component become [`RepoError::Conflict`]; missing rows become
/// [`RepoError::NotFound`]; everything else stays an opaque [`RepoError::Storage`]
/// (thread 7 keeps *semantic* misses — e.g. a missing target folder — explicit
/// `NotFound` at the query level, never via this fallback).
pub(crate) fn map_rusqlite(e: rusqlite::Error) -> RepoError {
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

/// Wall-clock timestamp for soft-delete stamps. SQLite-side default; use cases
/// normally supply timestamps via the Clock port, deletion here just needs a
/// monotone-enough marker.
pub(crate) fn now_stamp() -> String {
    // Fallback when the caller cannot inject a clock: seconds-precision UTC.
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| format!("{}-sym", d.as_secs()))
        .unwrap_or_else(|_| "deleted-sym".into())
}
