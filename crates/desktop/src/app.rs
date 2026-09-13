//! Framework-free shell logic: the three-pane state machine and the use-case
//! bundle the shell executes.
//!
//! Pure logic over the presentation view models: no egui, no display, fully
//! unit-testable headless (see the tests at the bottom and in
//! `tests/desktop_behavior.rs`). `crate::views` renders this state; `main.rs`
//! owns the event loop.

use rusty_notes_application::use_cases::folders::{
    CreateFolder, DeleteFolder, ListFolders, RenameFolder,
};
use rusty_notes_application::use_cases::notes::{
    CreateNote, DeleteNote, ListNotes, MoveNote, OpenNote, SearchNotes, UpdateNote,
};
use rusty_notes_domain::{FolderId, Note, NoteDraft, NoteId};
use rusty_notes_presentation::{folder_list_items, EditorState, FolderListItem, NoteListItem};

/// What the shell should persist when the user commits the editor.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SaveRequest {
    pub note_id: String,
    pub title: String,
    pub body: String,
}

/// Confirmation the shell should ask for before executing a deletion.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PendingConfirm {
    DeleteNote(String),
    DeleteFolder(String),
}

/// All state and transitions of the three-pane shell (folders | list | editor).
#[derive(Debug, Clone, Default)]
pub struct AppState {
    /// Selected folder; `None` means "All notes".
    pub selected_folder: Option<String>,
    pub folders: Vec<FolderListItem>,
    pub notes: Vec<NoteListItem>,
    pub selected_note: Option<String>,
    pub editor: EditorState,
    pub search_query: String,
    /// `true` while a search is active (middle pane shows results, not folder listing).
    pub search_active: bool,
    pub pending_confirm: Option<PendingConfirm>,
    pub error: Option<String>,
}

impl AppState {
    pub fn select_folder(&mut self, id: Option<String>) {
        self.selected_folder = id;
        self.search_active = false;
        self.search_query.clear();
    }

    /// Selects a note in the middle pane. The editor is NOT populated here: the
    /// full note (title AND body) is loaded through the `OpenNote` use case and
    /// applied via [`AppState::apply_opened_note`] (thread 1: the list carries
    /// only display data, so populating from it would wipe the body).
    pub fn select_note(&mut self, id: &str) {
        if self.notes.iter().any(|n| n.id == id) {
            self.selected_note = Some(id.to_string());
        }
    }

    /// Opens a fetched note in the editor (fresh from the repository = not dirty).
    /// Ignored if the selection changed while the note was being loaded.
    pub fn apply_opened_note(&mut self, note: &Note) {
        if self.selected_note.as_deref() == Some(note.id.as_str()) {
            self.editor = EditorState::open(note);
        }
    }

    pub fn begin_search(&mut self, query: &str) {
        self.search_query = query.to_string();
        self.search_active = !query.trim().is_empty();
    }

    /// Applies search hits (already resolved to notes by the `SearchNotes` use case).
    pub fn apply_search_results(&mut self, notes: Vec<NoteListItem>) {
        self.notes = notes;
    }

    pub fn edit_title(&mut self, title: String) {
        self.editor.title = title;
        self.editor.dirty = true;
    }

    pub fn edit_body(&mut self, body: String) {
        self.editor.body = body;
        self.editor.dirty = true;
    }

    /// The editor holds an open note and unsaved changes.
    pub fn save_requested(&self) -> Option<SaveRequest> {
        let note_id = self.editor.note_id.as_ref()?;
        self.editor.dirty.then(|| SaveRequest {
            note_id: note_id.clone(),
            title: self.editor.title.clone(),
            body: self.editor.body.clone(),
        })
    }

    /// Marks the editor clean after a successful save.
    pub fn saved(&mut self) {
        self.editor.saved();
    }

    pub fn request_delete_note(&mut self, id: &str) {
        self.pending_confirm = Some(PendingConfirm::DeleteNote(id.to_string()));
    }

    pub fn request_delete_folder(&mut self, id: &str) {
        self.pending_confirm = Some(PendingConfirm::DeleteFolder(id.to_string()));
    }

    pub fn confirm_cancel(&mut self) {
        self.pending_confirm = None;
    }

    pub fn set_error(&mut self, message: String) {
        self.error = Some(message);
    }

    /// Thread 5: the error banner is dismissible; this clears the current error.
    pub fn clear_error(&mut self) {
        self.error = None;
    }

    /// Applies a fresh listing to the middle pane, preserving selection when the
    /// selected note is still present.
    pub fn apply_note_listing(&mut self, notes: Vec<NoteListItem>) {
        let keep = self
            .selected_note
            .as_ref()
            .is_some_and(|sel| notes.iter().any(|n| &n.id == sel));
        if !keep {
            self.selected_note = None;
        }
        self.notes = notes;
    }
}

/// The use-case bundle the shell executes (wired in [`crate::composition`]).
pub struct Actions {
    pub list_notes: ListNotes,
    pub list_folders: ListFolders,
    /// Loads the full note (title + body) when the user opens one (thread 1).
    pub open_note: OpenNote,
    pub create_note: CreateNote,
    pub update_note: UpdateNote,
    pub delete_note: DeleteNote,
    /// Move affordance: the shell calls it when the user drops a note onto a
    /// folder row (or picks a target from the note context menu).
    pub move_note: MoveNote,
    pub create_folder: CreateFolder,
    pub rename_folder: RenameFolder,
    pub delete_folder: DeleteFolder,
    pub search: SearchNotes,
}

impl Actions {
    /// Reloads folders and notes into `state` according to the current selection.
    pub fn refresh(&self, state: &mut AppState) -> Result<(), String> {
        let folders = self.list_folders.execute().map_err(|e| e.to_string())?;
        let all = self.list_notes.execute(None).map_err(|e| e.to_string())?;
        let mut counts = std::collections::HashMap::new();
        for n in &all {
            *counts.entry(n.folder_id.0.clone()).or_default() += 1;
        }
        state.folders = folder_list_items(&folders, &counts, state.selected_folder.as_deref());
        let notes = match &state.selected_folder {
            Some(id) => self
                .list_notes
                .execute(Some(FolderId(id.clone())))
                .map_err(|e| e.to_string())?,
            None => all,
        };
        let selected = state.selected_note.clone();
        state.apply_note_listing(rusty_notes_presentation::note_list_items(
            &notes,
            selected.as_deref(),
        ));
        Ok(())
    }

    /// Opens a note: selects it and loads the FULL note (title and body) into the
    /// editor via the `OpenNote` use case (thread 1 regression guard).
    pub fn open_note(&self, state: &mut AppState, id: &str) -> Result<(), String> {
        state.select_note(id);
        match self
            .open_note
            .execute(&NoteId(id.to_string()))
            .map_err(|e| e.to_string())?
        {
            Some(note) => state.apply_opened_note(&note),
            None => {
                // Stale listing: the note is gone (soft-deleted via another path or
                // purged). Drop the dead selection instead of showing a ghost
                // editor; the next refresh heals the list (thread 6 UI side).
                state.selected_note = None;
                state.editor = EditorState::default();
                return Err("note not found".to_string());
            }
        }
        Ok(())
    }

    pub fn create_note(&self, state: &mut AppState, title: &str, body: &str) -> Result<(), String> {
        let folder = state
            .selected_folder
            .clone()
            .ok_or("select a folder first")?;
        let draft = NoteDraft {
            folder_id: FolderId(folder),
            title: title.to_string(),
            body: body.to_string(),
        };
        self.create_note.execute(draft).map_err(|e| e.to_string())?;
        self.refresh(state)
    }

    /// Saves the dirty editor state. The request always carries the editor's full
    /// title AND body (which `open_note` loaded from storage), so a title-only
    /// save can never wipe the stored body (thread 1).
    pub fn save(&self, state: &mut AppState) -> Result<(), String> {
        match state.save_requested() {
            None => Ok(()), // nothing dirty: explicit save is a no-op
            Some(SaveRequest {
                note_id,
                title,
                body,
            }) => {
                self.update_note
                    .execute(&NoteId(note_id), Some(title), Some(body))
                    .map_err(|e| e.to_string())?;
                state.saved();
                self.refresh(state)
            }
        }
    }

    pub fn delete_note_confirmed(&self, state: &mut AppState, id: &str) -> Result<(), String> {
        self.delete_note
            .execute(&NoteId(id.to_string()))
            .map_err(|e| e.to_string())?;
        if state.selected_note.as_deref() == Some(id) {
            state.selected_note = None;
            state.editor = EditorState::default();
        }
        self.refresh(state)
    }

    pub fn delete_folder_confirmed(&self, state: &mut AppState, id: &str) -> Result<(), String> {
        self.delete_folder
            .execute(&FolderId(id.to_string()))
            .map_err(|e| e.to_string())?;
        if state.selected_folder.as_deref() == Some(id) {
            state.select_folder(None);
        }
        self.refresh(state)
    }

    /// Moves a note to a target folder and refreshes the listing. Reachable from
    /// the note context menu in the shell; kept public and tested via use-case
    /// integration so the feature exists even without drag-and-drop.
    pub fn move_note(&self, state: &mut AppState, id: &str, target: &str) -> Result<(), String> {
        self.move_note
            .execute(&NoteId(id.to_string()), FolderId(target.to_string()))
            .map_err(|e| e.to_string())?;
        self.refresh(state)
    }

    pub fn run_search(&self, state: &mut AppState) -> Result<(), String> {
        let query = state.search_query.clone();
        if query.trim().is_empty() {
            state.begin_search("");
            return self.refresh(state);
        }
        state.begin_search(&query);
        let hits = self.search.execute(&query).map_err(|e| e.to_string())?;
        state.apply_search_results(rusty_notes_presentation::note_list_items(
            &hits,
            state.selected_note.as_deref(),
        ));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn item(id: &str, folder_id: &str, selected: bool) -> NoteListItem {
        NoteListItem {
            id: id.into(),
            folder_id: folder_id.into(),
            title: "t".into(),
            preview: String::new(),
            updated_at: String::new(),
            selected,
        }
    }

    #[test]
    fn selection_and_search_flags() {
        let mut s = AppState::default();
        s.select_folder(Some("f1".into()));
        assert_eq!(s.selected_folder.as_deref(), Some("f1"));
        assert!(!s.search_active);
        s.begin_search("budget");
        assert!(s.search_active);
        s.select_folder(None);
        assert!(!s.search_active, "selecting a folder clears search mode");
        assert!(s.search_query.is_empty());
    }

    #[test]
    fn select_note_only_selects_listed_notes() {
        let mut s = AppState {
            notes: vec![item("n1", "f1", false)],
            ..AppState::default()
        };
        s.select_note("nope");
        assert!(s.selected_note.is_none(), "unknown id must not select");
        s.select_note("n1");
        assert_eq!(s.selected_note.as_deref(), Some("n1"));
        // Thread 1: selection alone must NOT fabricate editor content.
        assert!(s.editor.note_id.is_none());
        assert!(s.editor.body.is_empty());
    }

    #[test]
    fn opened_note_populates_full_editor() {
        let mut s = AppState {
            selected_note: Some("n1".into()),
            ..AppState::default()
        };
        let note = Note::new(
            NoteDraft {
                folder_id: FolderId("f1".into()),
                title: "t".into(),
                body: "the body".into(),
            },
            NoteId("n1".into()),
            rusty_notes_domain::Timestamp("2026-09-13T12:00:00Z".into()),
        )
        .unwrap();
        s.apply_opened_note(&note);
        assert_eq!(s.editor.note_id.as_deref(), Some("n1"));
        assert_eq!(s.editor.title, "t");
        assert_eq!(s.editor.body, "the body");
        assert!(!s.editor.dirty, "freshly opened note is not dirty");
    }

    #[test]
    fn opened_note_ignored_if_selection_moved_on() {
        let mut s = AppState {
            notes: vec![item("n1", "f1", false), item("n2", "f1", false)],
            ..AppState::default()
        };
        s.select_note("n1");
        s.select_note("n2"); // user clicks the next note while n1 loads
        let stale = Note::new(
            NoteDraft {
                folder_id: FolderId("f1".into()),
                title: "t".into(),
                body: "stale".into(),
            },
            NoteId("n1".into()),
            rusty_notes_domain::Timestamp("2026-09-13T12:00:00Z".into()),
        )
        .unwrap();
        s.apply_opened_note(&stale);
        assert_ne!(
            s.editor.note_id.as_deref(),
            Some("n1"),
            "stale open ignored"
        );
    }

    #[test]
    fn editor_dirty_tracking_drives_save_requests() {
        let mut s = AppState::default();
        assert!(s.save_requested().is_none(), "nothing open");
        s.editor = EditorState {
            note_id: Some("n1".into()),
            ..EditorState::default()
        };
        assert!(s.save_requested().is_none(), "open but clean");
        s.edit_title("new title".into());
        s.edit_body("body".into());
        let req = s.save_requested().expect("dirty note");
        assert_eq!(req.note_id, "n1");
        assert_eq!(req.title, "new title");
        s.saved();
        assert!(s.save_requested().is_none(), "saved => clean");
    }

    #[test]
    fn pending_confirm_round_trip() {
        let mut s = AppState::default();
        s.request_delete_note("n1");
        assert_eq!(
            s.pending_confirm,
            Some(PendingConfirm::DeleteNote("n1".into()))
        );
        s.confirm_cancel();
        assert!(s.pending_confirm.is_none());
        s.request_delete_folder("f1");
        assert_eq!(
            s.pending_confirm,
            Some(PendingConfirm::DeleteFolder("f1".into()))
        );
    }

    #[test]
    fn apply_note_listing_drops_stale_selection() {
        let mut s = AppState {
            selected_note: Some("ghost".into()),
            ..AppState::default()
        };
        s.apply_note_listing(vec![item("n1", "f1", false)]);
        assert!(s.selected_note.is_none(), "stale selection must clear");
        let mut s = AppState {
            selected_note: Some("n1".into()),
            ..AppState::default()
        };
        s.apply_note_listing(vec![item("n1", "f1", true)]);
        assert_eq!(
            s.selected_note.as_deref(),
            Some("n1"),
            "live selection kept"
        );
    }
}
