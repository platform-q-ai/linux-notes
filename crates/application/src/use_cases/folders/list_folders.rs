//! `ListFolders`: lists folders in name order.

use std::sync::Arc;

use rusty_notes_domain::Folder;

use crate::error::AppError;
use crate::ports::NoteRepository;

/// Lists folders in name order (the repository's ordering contract).
pub struct ListFolders {
    repo: Arc<dyn NoteRepository>,
}

impl ListFolders {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self) -> Result<Vec<Folder>, AppError> {
        Ok(self.repo.list_folders()?)
    }
}
