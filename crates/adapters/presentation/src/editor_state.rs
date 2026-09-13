//! [`EditorState`]: state of the right-pane editor.

use rusty_notes_domain::Note;

/// State of the right-pane editor.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct EditorState {
    pub note_id: Option<String>,
    pub title: String,
    pub body: String,
    pub dirty: bool,
    pub error: Option<String>,
}

impl EditorState {
    /// Opens a note in the editor (fresh from the repository = not dirty).
    pub fn open(note: &Note) -> Self {
        Self {
            note_id: Some(note.id.0.clone()),
            title: note.title.clone(),
            body: note.body.clone(),
            dirty: false,
            error: None,
        }
    }

    /// Records an edit; `dirty` drives the Save button / autosave indicator.
    pub fn edit(&mut self, title: String, body: String) {
        self.title = title;
        self.body = body;
        self.dirty = true;
    }

    /// Marks the state as persisted.
    pub fn saved(&mut self) {
        self.dirty = false;
        self.error = None;
    }

    /// Records a save failure message for display.
    pub fn failed(&mut self, message: String) {
        self.error = Some(message);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusty_notes_domain::{FolderId, NoteDraft, NoteId, Timestamp};

    fn note(id: &str, title: &str, body: &str) -> Note {
        Note::new(
            NoteDraft {
                folder_id: FolderId("f1".into()),
                title: title.into(),
                body: body.into(),
            },
            NoteId(id.into()),
            Timestamp("2026-09-13T12:00:00Z".into()),
        )
        .unwrap()
    }

    #[test]
    fn editor_state_tracks_dirty_and_saved() {
        let n = note("a", "title", "body");
        let mut editor = EditorState::open(&n);
        assert!(!editor.dirty);
        assert_eq!(editor.title, "title");
        assert_eq!(editor.body, "body");
        editor.edit("new".into(), "new body".into());
        assert!(editor.dirty);
        editor.saved();
        assert!(!editor.dirty);
        editor.failed("boom".into());
        assert_eq!(editor.error.as_deref(), Some("boom"));
    }
}
