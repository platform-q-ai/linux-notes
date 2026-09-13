//! `SearchNotes`: searches notes and resolves hits to full notes, best match
//! first.

use std::sync::Arc;

use rusty_notes_domain::Note;

use crate::error::AppError;
use crate::ports::{NoteRepository, SearchService};

/// Searches notes and resolves hits to full notes, best match first. Matching
/// semantics (whole tokens, cap, ordering) are pinned by the shared search
/// contract both implementations must pass (behavior thread 3).
pub struct SearchNotes {
    search: Arc<dyn SearchService>,
    repo: Arc<dyn NoteRepository>,
}

impl SearchNotes {
    pub fn new(search: Arc<dyn SearchService>, repo: Arc<dyn NoteRepository>) -> Self {
        Self { search, repo }
    }

    pub fn execute(&self, query: &str) -> Result<Vec<Note>, AppError> {
        let ids = self.search.search(query)?;
        let mut notes = Vec::with_capacity(ids.len());
        for id in ids {
            // The index only contains live notes; a missing row is a lagging index,
            // not a user-facing error.
            if let Some(note) = self.repo.get_note(&id)? {
                notes.push(note);
            }
        }
        Ok(notes)
    }
}
