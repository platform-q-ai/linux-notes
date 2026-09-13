//! The in-memory fakes must pass the very same behavioral contracts the real
//! adapters are held to (`contracts::run_all` / `contracts::run_all_search`).
//! This suite keeps the fakes honest on their own; the SQLite adapter runs the
//! identical suites in its integration tests.

use std::sync::Arc;

use rusty_notes_application::ports::{NoteRepository, SearchService};
use rusty_notes_test_support::contracts::{run_all, run_all_search};
use rusty_notes_test_support::fakes::{InMemoryNoteRepository, InMemorySearch};

fn make_repo() -> InMemoryNoteRepository {
    InMemoryNoteRepository::default()
}

fn make_search_pair() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>) {
    let repo = Arc::new(InMemoryNoteRepository::default());
    let search = Arc::new(InMemorySearch::new(InMemoryNoteRepository::clone(&repo)));
    (repo, search)
}

#[test]
fn in_memory_repository_passes_the_shared_repository_contract() {
    run_all(make_repo);
}

#[test]
fn in_memory_search_passes_the_shared_search_contract() {
    run_all_search(make_search_pair);
}
