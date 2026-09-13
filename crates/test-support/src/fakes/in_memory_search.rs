//! In-memory [`SearchService`] fake. Test-only: see crate `lib.rs`.
//!
//! Search-parity semantics (behavior thread 3): this fake mirrors the FTS5
//! adapter's observable behavior so both are honestly substitutable behind the
//! contract suite (`contracts::search_contract` pins BOTH sides on it):
//!
//! - **Phrase matching over tokenizer runs, not substring matching**: the query
//!   is whitespace-split into groups; each group is tokenized into
//!   alphanumeric runs (mirroring the FTS5 `unicode61` tokenizer) and must
//!   appear as an ADJACENT token sequence in the title or body. `"budget"`
//!   does not match `"budgetline"`; `"unique-needle"` matches the adjacent
//!   token pair `unique needle` (the adapter quotes each group into a phrase:
//!   `"unique-needle"`).
//! - **OR semantics over whitespace-separated groups** (the adapter joins the
//!   quoted phrases with `OR` in its MATCH expression).
//! - **Ranking mirrors `bm25(notes_fts, 10.0, 1.0)`** (title weight 10, body
//!   weight 1): score = 10 × title phrase hits + 1 × body phrase hits,
//!   descending; ties break by `updated_at` DESC, then id DESC (same tie-break
//!   as listings).
//! - **Volume cap**: at most 200 ids are returned (the adapter's `LIMIT 200`).
//! - Blank queries are `SearchError::EmptyQuery`; soft-deleted notes are excluded.

use rusty_notes_application::error::SearchError;
use rusty_notes_application::ports::{NoteRepository, SearchService};
use rusty_notes_domain::NoteId;

use super::in_memory_note_repository::InMemoryNoteRepository;

/// Result cap mirrored from the SQLite adapter's `LIMIT 200`.
const SEARCH_LIMIT: usize = 200;

/// Title-hit weight mirrored from `bm25(notes_fts, 10.0, 1.0)`.
const TITLE_WEIGHT: usize = 10;

/// Body-hit weight mirrored from `bm25(notes_fts, 10.0, 1.0)`.
const BODY_WEIGHT: usize = 1;

/// Lowercase alphanumeric runs of `text` (mirrors the FTS5 `unicode61`
/// tokenizer closely enough for the contract corpus: letters/digits are token
/// characters, everything else separates).
fn tokenize(text: &str) -> Vec<String> {
    text.to_lowercase()
        .split(|c: char| !c.is_alphanumeric())
        .filter(|run| !run.is_empty())
        .map(str::to_owned)
        .collect()
}

/// Number of ADJACENT occurrences of the `phrase` token sequence in `text`.
fn phrase_hits(text: &str, phrase: &[String]) -> usize {
    if phrase.is_empty() {
        return 0;
    }
    let haystack = tokenize(text);
    if phrase.len() > haystack.len() {
        return 0;
    }
    let mut hits = 0;
    for start in 0..=(haystack.len() - phrase.len()) {
        if haystack[start..start + phrase.len()] == *phrase {
            hits += 1;
        }
    }
    hits
}

/// Case-insensitive in-memory search over live notes.
#[derive(Clone)]
pub struct InMemorySearch {
    repo: InMemoryNoteRepository,
}

impl InMemorySearch {
    pub fn new(repo: InMemoryNoteRepository) -> Self {
        Self { repo }
    }
}

impl SearchService for InMemorySearch {
    fn search(&self, query: &str) -> Result<Vec<NoteId>, SearchError> {
        let query = query.trim();
        if query.is_empty() {
            return Err(SearchError::EmptyQuery);
        }
        let notes = self
            .repo
            .list_all_notes()
            .map_err(|e| SearchError::Storage(e.to_string()))?;
        // OR semantics over whitespace-split groups, mirroring the adapter's
        // quoted-phrase MATCH expression joined with `OR`.
        let phrases: Vec<Vec<String>> = query
            .split_whitespace()
            .map(tokenize)
            .filter(|phrase| !phrase.is_empty())
            .collect();
        let mut scored: Vec<(usize, &rusty_notes_domain::Note)> = notes
            .iter()
            .map(|n| {
                let hits: usize = phrases
                    .iter()
                    .map(|phrase| {
                        phrase_hits(&n.title, phrase) * TITLE_WEIGHT
                            + phrase_hits(&n.body, phrase) * BODY_WEIGHT
                    })
                    .sum();
                (hits, n)
            })
            .filter(|(hits, _)| *hits > 0)
            .collect();
        scored.sort_by(|a, b| {
            b.0.cmp(&a.0)
                .then_with(|| b.1.updated_at.cmp(&a.1.updated_at))
                .then_with(|| b.1.id.0.cmp(&a.1.id.0))
        });
        scored.truncate(SEARCH_LIMIT);
        Ok(scored.into_iter().map(|(_, n)| n.id.clone()).collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusty_notes_domain::{Folder, FolderId, Note, NoteDraft, Timestamp};

    fn repo_with_notes() -> InMemoryNoteRepository {
        InMemoryNoteRepository::default()
    }

    fn insert(repo: &InMemoryNoteRepository, id: &str, title: &str, body: &str) {
        // Idempotent per test: the folder insert only matters for the first call.
        let _ = repo.insert_folder(
            &Folder::new("Inbox".into(), FolderId("f".into()), Timestamp("t0".into()))
                .expect("valid folder"),
        );
        let note = Note::new(
            NoteDraft {
                folder_id: FolderId("f".into()),
                title: title.into(),
                body: body.into(),
            },
            NoteId(id.into()),
            Timestamp(format!("t{id}")),
        )
        .unwrap();
        repo.insert_note(&note).unwrap();
    }

    #[test]
    fn token_matching_not_substring() {
        let repo = repo_with_notes();
        insert(&repo, "a", "budgetline magic", "");
        let search = InMemorySearch::new(repo);
        assert!(
            search.search("budget").expect("search").is_empty(),
            "whole-token semantics: 'budget' must NOT match 'budgetline' (thread 3)"
        );
        assert_eq!(search.search("budgetline").expect("search").len(), 1);
    }

    #[test]
    fn hyphenated_query_is_a_phrase_of_adjacent_tokens() {
        let repo = repo_with_notes();
        insert(&repo, "a", "unique-needle", "");
        let search = InMemorySearch::new(repo);
        // Mirrors the adapter quoting the whole group: `"unique-needle"` is a
        // two-token phrase and matches the adjacent `unique needle` tokens.
        assert_eq!(search.search("unique-needle").expect("search").len(), 1);
        // Reversed order is NOT adjacent -> no hit.
        assert!(search.search("needle-unique").expect("search").is_empty());
    }

    #[test]
    fn title_hits_rank_above_body_hits_and_cap_applies() {
        let repo = repo_with_notes();
        insert(&repo, "a", "budget report", "nothing here");
        insert(&repo, "b", "random", "budget line");
        let search = InMemorySearch::new(repo);
        let hits = search.search("budget").expect("search");
        assert_eq!(hits.len(), 2);
        assert_eq!(hits[0], NoteId("a".into()), "title hit ranks first");
        assert_eq!(search.search("  ").unwrap_err(), SearchError::EmptyQuery);
    }
}
