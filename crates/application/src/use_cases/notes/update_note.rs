//! `UpdateNote`: updates title and/or body of an existing note through domain
//! operations.

use std::sync::Arc;

use rusty_notes_domain::{Note, NoteId};

use crate::error::AppError;
use crate::ports::{Clock, NoteRepository};

/// Updates title and/or body of an existing note through domain operations.
/// Invariants run on every path: a body over `MAX_BODY_BYTES` is rejected by
/// `Note::edit_body` (behavior thread 4), and `updated_at` only moves for an
/// accepted edit.
pub struct UpdateNote {
    repo: Arc<dyn NoteRepository>,
    clock: Arc<dyn Clock>,
}

impl UpdateNote {
    pub fn new(repo: Arc<dyn NoteRepository>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, clock }
    }

    pub fn execute(
        &self,
        id: &NoteId,
        title: Option<String>,
        body: Option<String>,
    ) -> Result<Note, AppError> {
        let mut note = self.repo.get_note(id)?.ok_or(AppError::NoteMissing)?;
        if let Some(title) = title {
            note.rename(title, self.clock.now())?;
        }
        if let Some(body) = body {
            note.edit_body(body, self.clock.now())?;
        }
        self.repo.update_note(&note)?;
        Ok(note)
    }
}
