//! Search port.

use rusty_notes_domain::NoteId;

use crate::error::SearchError;

/// Search port. Returns matching note ids, best match first, excluding
/// soft-deleted notes.
///
/// Observable semantics (pinned by the shared search contract in `test-support`,
/// which both the in-memory fake and the SQLite FTS5 adapter must pass —
/// behavior thread 3):
/// - the query is whitespace-split into tokens; a note matches when ANY token
///   occurs as a whole token (not a substring) in its title or body,
///   case-insensitively;
/// - ranking weights title matches above body-only matches;
/// - implementations may cap the number of returned ids (SQLite uses
///   `LIMIT 200`); the contract pins that cap for every implementation;
/// - blank (empty or whitespace-only) queries yield [`SearchError::EmptyQuery`].
pub trait SearchService: Send + Sync {
    fn search(&self, query: &str) -> Result<Vec<NoteId>, SearchError>;
}
