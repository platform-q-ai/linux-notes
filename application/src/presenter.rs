//! Framework-free presenter types: plain data the desktop shell renders.
//!
//! Keeping view models here (not in `app/desktop`) lets the mapping from domain
//! types be unit-tested headless with no egui involved.

use rusty_notes_domain::{Folder, Note};

/// One row in the middle-pane note list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NoteListItem {
    pub id: String,
    pub folder_id: String,
    pub title: String,
    pub preview: String,
    pub updated_at: String,
    pub selected: bool,
}

/// One row in the left-pane folder list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FolderListItem {
    pub id: String,
    pub name: String,
    pub note_count: usize,
    pub selected: bool,
}

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

/// Builds the note-list rows for a folder (or all notes) plus the current selection.
pub fn note_list_items(notes: &[Note], selected: Option<&str>) -> Vec<NoteListItem> {
    notes
        .iter()
        .map(|n| NoteListItem {
            id: n.id.0.clone(),
            folder_id: n.folder_id.0.clone(),
            title: if n.title.trim().is_empty() {
                "Untitled".to_string()
            } else {
                n.title.clone()
            },
            preview: {
                let flat: String = n
                    .body
                    .chars()
                    .map(|c| if c == '\n' { ' ' } else { c })
                    .collect();
                let mut preview: String = flat.chars().take(80).collect();
                if flat.chars().count() > 80 {
                    preview.push('…');
                }
                preview
            },
            updated_at: n.updated_at.0.clone(),
            selected: selected.is_some_and(|s| s == n.id.0),
        })
        .collect()
}

/// Builds the folder-list rows given folders and per-folder live note counts.
pub fn folder_list_items(
    folders: &[Folder],
    counts: &std::collections::HashMap<String, usize>,
    selected: Option<&str>,
) -> Vec<FolderListItem> {
    folders
        .iter()
        .map(|f| FolderListItem {
            id: f.id.0.clone(),
            name: f.name.clone(),
            note_count: counts.get(&f.id.0).copied().unwrap_or(0),
            selected: selected.is_some_and(|s| s == f.id.0),
        })
        .collect()
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
    fn note_items_flag_selection_and_truncate_preview() {
        let notes = vec![note("a", "Alpha", "long body ".repeat(20).trim()), {
            // Presenters must tolerate an empty title (e.g. legacy data);
            // the domain constructor refuses it, so craft it directly.
            let mut untitled = note("b", "temp", "");
            untitled.title = String::new();
            untitled
        }];
        let items = note_list_items(&notes, Some("b"));
        assert_eq!(items.len(), 2);
        assert!(!items[0].selected);
        assert!(items[1].selected);
        assert_eq!(items[1].title, "Untitled");
        assert!(items[0].preview.ends_with('…'));
        assert!(items[0].preview.chars().count() <= 81);
    }

    #[test]
    fn editor_state_tracks_dirty_and_saved() {
        let n = note("a", "title", "body");
        let mut editor = EditorState::open(&n);
        assert!(!editor.dirty);
        editor.edit("new".into(), "new body".into());
        assert!(editor.dirty);
        editor.saved();
        assert!(!editor.dirty);
        editor.failed("boom".into());
        assert_eq!(editor.error.as_deref(), Some("boom"));
    }

    #[test]
    fn folder_items_carry_counts_and_selection() {
        let folders = vec![
            Folder::new("Inbox".into(), FolderId("f1".into()), Timestamp("t".into())).unwrap(),
            Folder::new("Work".into(), FolderId("f2".into()), Timestamp("t".into())).unwrap(),
        ];
        let mut counts = std::collections::HashMap::new();
        counts.insert("f1".to_string(), 3);
        let items = folder_list_items(&folders, &counts, Some("f2"));
        assert_eq!(items[0].note_count, 3);
        assert_eq!(items[1].note_count, 0);
        assert!(items[1].selected);
        assert!(!items[0].selected);
    }
}
