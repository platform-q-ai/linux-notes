//! FTS5-backed [`SearchService`].

use std::sync::{Arc, Mutex};

use rusqlite::Connection;
use rusty_notes_application::error::SearchError;
use rusty_notes_application::ports::SearchService;
use rusty_notes_domain::NoteId;

/// FTS5-backed [`SearchService`]. Blank queries map to [`SearchError::EmptyQuery`];
/// user input is always passed as a parameter, and bare tokens are quoted for the
/// MATCH expression so quotes and hyphens are treated literally.
///
/// Observable semantics are pinned by the shared search contract
/// (`rusty_notes_test_support::contracts::run_all_search`, run against this
/// adapter in `tests/search_contract.rs`): whitespace-split groups OR'd together,
/// whole-token (not substring) matching via the `unicode61` tokenizer, title hits
/// ranked above body-only hits (bm25 weights 10 : 1), results capped at 200, and
/// soft-deleted notes excluded.
pub struct SqliteSearchService {
    conn: Arc<Mutex<Connection>>,
}

impl SqliteSearchService {
    pub fn new(conn: Connection) -> Self {
        Self {
            conn: Arc::new(Mutex::new(conn)),
        }
    }

    pub fn shared(conn: Arc<Mutex<Connection>>) -> Self {
        Self { conn }
    }
}

/// Wraps each whitespace-separated group in double quotes and ORs them, escaping
/// embedded quotes by doubling. `rusty "notes"` -> `"rusty" OR "\"notes\""`.
/// Hyphenated groups like `todo-list` become quoted PHRASES (adjacent token
/// sequences) — they must NOT be turned into column filters or NOT operators.
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
