//! `DeleteNote`: soft-deletes a note (recoverable until purged).

use std::sync::Arc;

use rusty_notes_domain::NoteId;

use crate::error::AppError;
use crate::ports::NoteRepository;

/// Soft-deletes a note: it disappears from listings and search and plain reads,
/// and stays recoverable through `get_note_including_deleted` until purged
/// (behavior thread 6). Explicitly deleting a missing note is
/// `AppError::Repo(RepoError::NotFound)`.
pub struct DeleteNote {
    repo: Arc<dyn NoteRepository>,
}

impl DeleteNote {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self, id: &NoteId) -> Result<(), AppError> {
        self.repo.soft_delete_note(id)?;
        Ok(())
    }
}
