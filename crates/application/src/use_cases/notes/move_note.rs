//! `MoveNote`: moves a note into another folder (Apple-Notes-style drag between
//! folders).

use std::sync::Arc;

use rusty_notes_domain::{FolderId, NoteId};

use crate::error::AppError;
use crate::ports::{Clock, NoteRepository};

/// Moves a note into another folder (Apple-Notes-style drag between folders).
pub struct MoveNote {
    repo: Arc<dyn NoteRepository>,
    clock: Arc<dyn Clock>,
}

impl MoveNote {
    pub fn new(repo: Arc<dyn NoteRepository>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, clock }
    }

    pub fn execute(
        &self,
        id: &NoteId,
        target: FolderId,
    ) -> Result<rusty_notes_domain::Note, AppError> {
        if self.repo.get_folder(&target)?.is_none() {
            return Err(AppError::FolderMissing);
        }
        let mut note = self.repo.get_note(id)?.ok_or(AppError::NoteMissing)?;
        note.move_to(target, self.clock.now());
        self.repo.update_note(&note)?;
        Ok(note)
    }
}
