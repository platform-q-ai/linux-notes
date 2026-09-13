//! Integration test: the in-memory fake passes the shared contract suite.
//!
//! The SQLite adapter runs this exact suite through its own factory
//! (`infrastructure/sqlite/tests/sqlite_contract.rs`) — same cases, real backend.

use rusty_notes_application::contract::{run_all, run_all_search};
use rusty_notes_application::memory::{InMemoryNoteRepository, InMemorySearch};
use rusty_notes_application::ports::SearchService;
use std::sync::Arc;

#[test]
fn in_memory_repository_honors_the_contract() {
    run_all(InMemoryNoteRepository::default);
}

#[test]
fn in_memory_search_honors_the_search_contract() {
    run_all_search(|| {
        let repo = Arc::new(InMemoryNoteRepository::default());
        let search: Arc<dyn SearchService> =
            Arc::new(InMemorySearch::new(InMemoryNoteRepository::clone(&repo)));
        (repo, search)
    });
}
