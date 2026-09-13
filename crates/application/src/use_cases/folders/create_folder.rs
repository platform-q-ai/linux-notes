//! `CreateFolder`: creates a folder.

use std::sync::Arc;

use rusty_notes_domain::Folder;

use crate::error::AppError;
use crate::ports::{Clock, Ids, NoteRepository};

/// Creates a folder; the non-empty name invariant is enforced by the domain.
pub struct CreateFolder {
    repo: Arc<dyn NoteRepository>,
    ids: Arc<dyn Ids>,
    clock: Arc<dyn Clock>,
}

impl CreateFolder {
    pub fn new(repo: Arc<dyn NoteRepository>, ids: Arc<dyn Ids>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, ids, clock }
    }

    pub fn execute(&self, name: String) -> Result<Folder, AppError> {
        let id = self.ids.next_folder_id();
        let folder = Folder::new(name, id, self.clock.now())?;
        self.repo.insert_folder(&folder)?;
        Ok(folder)
    }
}
