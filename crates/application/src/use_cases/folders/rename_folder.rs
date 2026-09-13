//! `RenameFolder`: renames a folder through the domain invariant.

use std::sync::Arc;

use rusty_notes_domain::FolderId;

use crate::error::AppError;
use crate::ports::{Clock, NoteRepository};

/// Renames a folder through the domain invariant (blank names are a domain
/// error; `updated_at` only moves for an accepted rename).
pub struct RenameFolder {
    repo: Arc<dyn NoteRepository>,
    clock: Arc<dyn Clock>,
}

impl RenameFolder {
    pub fn new(repo: Arc<dyn NoteRepository>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, clock }
    }

    pub fn execute(
        &self,
        id: &FolderId,
        name: String,
    ) -> Result<rusty_notes_domain::Folder, AppError> {
        let mut folder = self.repo.get_folder(id)?.ok_or(AppError::FolderMissing)?;
        folder.rename(name, self.clock.now())?;
        self.repo.update_folder(&folder)?;
        Ok(folder)
    }
}
