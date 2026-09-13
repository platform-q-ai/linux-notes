//! Mapping functions from domain entities to the list view models
//! ([`NoteListItem`], [`FolderListItem`]).

use std::collections::HashMap;

use rusty_notes_domain::{Folder, Note};

use crate::folder_list_item::FolderListItem;
use crate::note_list_item::NoteListItem;

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
    counts: &HashMap<String, usize>,
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
    fn folder_items_carry_counts_and_selection() {
        let folders = vec![
            Folder::new("Inbox".into(), FolderId("f1".into()), Timestamp("t".into())).unwrap(),
            Folder::new("Work".into(), FolderId("f2".into()), Timestamp("t".into())).unwrap(),
        ];
        let mut counts = HashMap::new();
        counts.insert("f1".to_string(), 3);
        let items = folder_list_items(&folders, &counts, Some("f2"));
        assert_eq!(items[0].note_count, 3);
        assert_eq!(items[1].note_count, 0);
        assert!(items[1].selected);
        assert!(!items[0].selected);
    }
}
