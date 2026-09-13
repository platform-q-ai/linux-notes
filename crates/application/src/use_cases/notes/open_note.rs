//! `OpenNote`: loads one full note (title AND body) by id.
//!
/// Behavior thread 1: the editor must open the real stored note, not a row
/// assembled from display data (which carries no body — assembling it wiped
/// stored bodies on the next save).
use std::sync::Arc;

use rusty_notes_domain::Note;

use crate::error::AppError;
use crate::ports::NoteRepository;

/// Loads one note in full. Returns `Ok(None)` when the note does not exist (or
/// is soft-deleted — plain reads expose live rows only; see the
/// `get_note_including_deleted` port doc for the recovery surface).
pub struct OpenNote {
    repo: Arc<dyn NoteRepository>,
}

impl OpenNote {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self, id: &rusty_notes_domain::NoteId) -> Result<Option<Note>, AppError> {
        Ok(self.repo.get_note(id)?)
    }
}
