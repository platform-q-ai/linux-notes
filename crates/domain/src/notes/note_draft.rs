//! Data required to create a new note.

use crate::FolderId;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NoteDraft {
    pub folder_id: FolderId,
    pub title: String,
    pub body: String,
}
