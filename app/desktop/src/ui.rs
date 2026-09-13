//! Framework-free three-pane UI state machine.
//!
//! Pure logic over the application presenter types: no egui, no display, fully
//! unit-testable headless. `crate::main` (composition root) renders this state.

use rusty_notes_application::presenter::{EditorState, NoteListItem};
use rusty_notes_application::use_cases::{
    CreateFolder, CreateNote, DeleteFolder, DeleteNote, ListFolders, ListNotes, MoveNote,
    SearchNotes, UpdateNote,
};
use rusty_notes_domain::{FolderId, NoteDraft, NoteId};

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
    pub folders: Vec<(String, String, usize)>, // (id, name, live note count)
    pub notes: Vec<NoteListItem>,
    pub selected_note: Option<String>,
    pub editor: EditorState,
    pub search_query: String,
    /// `Some(ids)` while a search is active (middle pane shows results, not folder listing).
    pub search_active: bool,
    pub pending_confirm: Option<PendingConfirm>,
    pub error: Option<String>,
}

impl AppState {
    pub fn select_folder(&mut self, id: Option<String>) {
        self.selected_folder = id.clone();
        self.search_active = false;
        self.search_query.clear();
        let _ = id;
    }

    pub fn select_note(&mut self, id: &str) {
        if let Some(item) = self.notes.iter().find(|n| n.id == id) {
            self.selected_note = Some(item.id.clone());
            self.editor = EditorState {
                note_id: Some(item.id.clone()),
                title: String::new(),
                body: String::new(),
                dirty: false,
                error: None,
            };
            // Title/preview are display data; the shell loads the full note via
            // the repository when opening. Dirty stays false until an edit.
            self.editor.title = item.title.clone();
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

/// Optional behavior bundle so the shell can be driven without egui in tests.
pub struct Actions {
    pub list_notes: ListNotes,
    pub list_folders: ListFolders,
    pub create_note: CreateNote,
    pub update_note: UpdateNote,
    pub delete_note: DeleteNote,
    /// Move affordance: the shell calls it when the user drops a note onto a
    /// folder row (or picks a target from the note context menu).
    pub move_note: MoveNote,
    pub create_folder: CreateFolder,
    pub delete_folder: DeleteFolder,
    pub search: SearchNotes,
}

impl Actions {
    /// Reloads folders and notes into `state` according to the current selection.
    pub fn refresh(&self, state: &mut AppState) -> Result<(), String> {
        let folders = self.list_folders.execute().map_err(|e| e.to_string())?;
        let all = self.list_notes.execute(None).map_err(|e| e.to_string())?;
        state.folders = folders
            .iter()
            .map(|f| {
                let count = all.iter().filter(|n| n.folder_id.0 == f.id.0).count();
                (f.id.0.clone(), f.name.clone(), count)
            })
            .collect();
        let notes = match &state.selected_folder {
            Some(id) => self
                .list_notes
                .execute(Some(FolderId(id.clone())))
                .map_err(|e| e.to_string())?,
            None => all,
        };
        let selected = state.selected_note.clone();
        state.apply_note_listing(rusty_notes_application::presenter::note_list_items(
            &notes,
            selected.as_deref(),
        ));
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
        self.create_note_use_case()
            .execute(draft)
            .map_err(|e| e.to_string())?;
        self.refresh(state)
    }

    // Small indirection to keep the struct field private-ish while reusing wiring.
    fn create_note_use_case(&self) -> &CreateNote {
        &self.create_note
    }

    pub fn save(&self, state: &mut AppState) -> Result<(), String> {
        match state.save_requested() {
            None => Ok(()), // nothing dirty: explicit save is a no-op
            Some(SaveRequest {
                note_id,
                title,
                body,
            }) => {
                let n = self
                    .update_note
                    .execute(&NoteId(note_id), Some(title), Some(body))
                    .map_err(|e| e.to_string())?;
                let _ = n;
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
        self.move_note_use_case()
            .execute(&NoteId(id.to_string()), FolderId(target.to_string()))
            .map_err(|e| e.to_string())?;
        self.refresh(state)
    }

    fn move_note_use_case(&self) -> &MoveNote {
        &self.move_note
    }

    pub fn run_search(&self, state: &mut AppState) -> Result<(), String> {
        let query = state.search_query.clone();
        if query.trim().is_empty() {
            state.begin_search("");
            return self.refresh(state);
        }
        state.begin_search(&query);
        let hits = self.search.execute(&query).map_err(|e| e.to_string())?;
        state.apply_search_results(rusty_notes_application::presenter::note_list_items(
            &hits,
            state.selected_note.as_deref(),
        ));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
        let items = vec![NoteListItem {
            id: "n1".into(),
            folder_id: "f1".into(),
            title: "t".into(),
            preview: String::new(),
            updated_at: String::new(),
            selected: false,
        }];
        s.apply_note_listing(items);
        assert!(s.selected_note.is_none(), "stale selection must clear");
        let mut s = AppState {
            selected_note: Some("n1".into()),
            ..AppState::default()
        };
        let items = vec![NoteListItem {
            id: "n1".into(),
            folder_id: "f1".into(),
            title: "t".into(),
            preview: String::new(),
            updated_at: String::new(),
            selected: true,
        }];
        s.apply_note_listing(items);
        assert_eq!(
            s.selected_note.as_deref(),
            Some("n1"),
            "live selection kept"
        );
    }
}
