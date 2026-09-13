//! `DeleteFolder`: deletes an empty folder only.

use std::sync::Arc;

use rusty_notes_domain::FolderId;

use crate::error::AppError;
use crate::ports::NoteRepository;

/// Deletes an empty folder only: `delete_folder` refuses with
/// `RepoError::FolderHasNotes` while any note — live or soft-deleted —
/// references it. Purging the deleted notes ("empty trash") clears the way;
/// without a purge a folder that ever held a deleted note would be permanently
/// undeletable (behavior thread 6).
pub struct DeleteFolder {
    repo: Arc<dyn NoteRepository>,
}

impl DeleteFolder {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self, id: &FolderId) -> Result<(), AppError> {
        self.repo.delete_folder(id)?;
        Ok(())
    }
}
