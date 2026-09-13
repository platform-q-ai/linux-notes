//! Shared search-parity contract run by EVERY `SearchService` adapter
//! (in-memory fake and SQLite alike). Test-only: see `lib.rs`.
//!
//! This suite is the thread-3 (inline thread 3999739437) fix: search parity is
//! no longer "the fake testing itself" — these cases pin the observable
//! semantics both implementations must agree on:
//!
//! - **Token matching, not substring matching**: query tokens match whole tokens
//!   (FTS5 `unicode61` tokenizer: alphanumeric runs); `"budget"` does not match
//!   `"budgetline"`.
//! - **OR semantics over whitespace-separated query tokens** (the adapter joins
//!   quoted tokens with `OR` in its MATCH expression).
//! - **Ranking direction mirrors `bm25(notes_fts, 10.0, 1.0)`**: title hits
//!   (weight 10) rank above body-only hits (weight 1); ties break by
//!   `updated_at` DESC, then id DESC — the same order listings use.
//! - **Volume cap**: at most 200 results (the adapter's `LIMIT 200`).
//! - Blank queries are `SearchError::EmptyQuery`; soft-deleted notes never match.

use std::sync::Arc;

use rusty_notes_application::error::SearchError;
use rusty_notes_application::ports::{NoteRepository, SearchService};
use rusty_notes_domain::{Folder, FolderId, Note, NoteDraft, NoteId, Timestamp};

const T0: &str = "2026-09-13T12:00:00Z";

/// Runs the full search contract against a factory of `(repo, search)` pairs
/// wired to the same underlying store. The repository is handed back so the
/// harness can populate data through the port.
pub fn run_all_search(make: impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>)) {
    let mut make = make;
    search_ranks_title_above_body_case(&mut make);
    search_token_boundary_not_substring_case(&mut make);
    search_matches_any_query_token_case(&mut make);
    search_blank_query_case(&mut make);
    search_excludes_deleted_case(&mut make);
    search_caps_results_case(&mut make);
}

/// Seeds a note through the repository port (fixture helper). One shared folder
/// per repository state is enough for search seeding.
fn search_seed(repo: &dyn NoteRepository, id: &str, title: &str, body: &str) -> Note {
    let folder = Folder::new(
        "Search".into(),
        FolderId("f-search".into()),
        Timestamp(T0.into()),
    )
    .expect("valid folder");
    match repo.insert_folder(&folder) {
        Ok(()) | Err(rusty_notes_application::error::RepoError::Conflict) => {
            // Freshly seeded, or already seeded by an earlier call: fine.
        }
        Err(e) => panic!("seed folder: {e:?}"),
    }
    let note = Note::new(
        NoteDraft {
            folder_id: FolderId("f-search".into()),
            title: title.into(),
            body: body.into(),
        },
        NoteId(id.into()),
        Timestamp(T0.into()),
    )
    .expect("valid note");
    repo.insert_note(&note).expect("insert note");
    note
}

fn search_ranks_title_above_body_case(
    make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>),
) {
    let (repo, search) = make();
    let title_hit = search_seed(repo.as_ref(), "n1", "budget report", "nothing here");
    let _body_hit = search_seed(repo.as_ref(), "n2", "random", "budget line");
    let hits = search.search("budget").expect("search ok");
    assert_eq!(hits.len(), 2, "both notes mention budget");
    // Title hits rank above body-only hits (bm25 weight parity).
    assert_eq!(hits[0], title_hit.id);
}

/// Thread 3 (parity): token-boundary matching is pinned for BOTH adapters.
fn search_token_boundary_not_substring_case(
    make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>),
) {
    let (repo, search) = make();
    let _n = search_seed(repo.as_ref(), "n1", "budgetline magic", "");
    assert!(
        search.search("budget").expect("search").is_empty(),
        "whole-token semantics: 'budget' must not match 'budgetline' on any adapter"
    );
    assert_eq!(search.search("budgetline").expect("search").len(), 1);
}

/// Thread 3 (parity): whitespace-separated tokens are OR'd, as in the SQLite
/// adapter's quoted-token MATCH expression.
fn search_matches_any_query_token_case(
    make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>),
) {
    let (repo, search) = make();
    let a = search_seed(repo.as_ref(), "n1", "alpha", "");
    let b = search_seed(repo.as_ref(), "n2", "beta", "");
    let hits = search.search("alpha beta").expect("search");
    assert_eq!(hits.len(), 2, "OR semantics over query tokens");
    assert!(hits.contains(&a.id));
    assert!(hits.contains(&b.id));
}

fn search_blank_query_case(
    make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>),
) {
    let (_repo, search) = make();
    let err = search.search("   ").expect_err("blank query");
    assert!(matches!(err, SearchError::EmptyQuery));
    let err = search.search("").expect_err("empty query");
    assert!(matches!(err, SearchError::EmptyQuery));
}

fn search_excludes_deleted_case(
    make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>),
) {
    let (repo, search) = make();
    let n = search_seed(repo.as_ref(), "n1", "unique-needle", "");
    assert_eq!(
        search.search("unique-needle").expect("hits"),
        vec![n.id.clone()]
    );
    repo.soft_delete_note(&n.id).expect("soft delete");
    assert!(
        search
            .search("unique-needle")
            .expect("search after delete")
            .is_empty(),
        "soft-deleted notes must not appear in search"
    );
}

/// Thread 3 (volume parity): results are capped at 200 on every adapter.
fn search_caps_results_case(
    make: &mut impl FnMut() -> (Arc<dyn NoteRepository>, Arc<dyn SearchService>),
) {
    let (repo, search) = make();
    for i in 0..210 {
        search_seed(repo.as_ref(), &format!("n{i}"), &format!("needle {i}"), "");
    }
    let hits = search.search("needle").expect("search");
    assert_eq!(hits.len(), 200, "search results are capped at 200");
}
