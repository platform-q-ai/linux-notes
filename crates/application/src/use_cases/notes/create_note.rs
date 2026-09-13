//! `CreateNote`: creates a note after validating the draft against domain
//! invariants.

use std::sync::Arc;

use rusty_notes_domain::{Note, NoteDraft};

use crate::error::AppError;
use crate::ports::{Clock, Ids, NoteRepository};

/// Creates a note after validating the draft against domain invariants.
pub struct CreateNote {
    repo: Arc<dyn NoteRepository>,
    ids: Arc<dyn Ids>,
    clock: Arc<dyn Clock>,
}

impl CreateNote {
    pub fn new(repo: Arc<dyn NoteRepository>, ids: Arc<dyn Ids>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, ids, clock }
    }

    pub fn execute(&self, draft: NoteDraft) -> Result<Note, AppError> {
        if self.repo.get_folder(&draft.folder_id)?.is_none() {
            return Err(AppError::FolderMissing);
        }
        let id = self.ids.next_note_id();
        let note = Note::new(draft, id, self.clock.now())?;
        self.repo.insert_note(&note)?;
        Ok(note)
    }
}
