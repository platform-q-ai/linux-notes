//! `ListNotes`: lists notes, optionally scoped to one folder.

use std::sync::Arc;

use rusty_notes_domain::{FolderId, Note};

use crate::error::AppError;
use crate::ports::NoteRepository;

/// Lists notes, optionally scoped to one folder. Ordering (`updated_at` DESC,
/// id DESC) is the repository's contract.
pub struct ListNotes {
    repo: Arc<dyn NoteRepository>,
}

impl ListNotes {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self, folder: Option<FolderId>) -> Result<Vec<Note>, AppError> {
        let notes = match folder {
            Some(id) => self.repo.list_notes_by_folder(&id)?,
            None => self.repo.list_all_notes()?,
        };
        Ok(notes)
    }
}
