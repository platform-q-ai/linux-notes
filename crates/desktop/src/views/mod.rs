//! Desktop rendering, split per pane: each view is a thin function from the
//! headless state ([`crate::app::AppState`]) plus the use-case bundle
//! ([`crate::app::Actions`]) to egui widgets. Views own no application state;
//! `eframe::run_native` stays in `main.rs`.

pub mod error_banner;
pub mod folder_sidebar;
pub mod note_editor;
pub mod note_list;

use std::sync::Arc;

use crate::app::{Actions, AppState, PendingConfirm};

/// The eframe shell: holds the headless state, the wired use cases, and the raw
/// text buffers of the two "new" forms. Rendering is delegated to the pane views.
pub struct DesktopApp {
    pub(crate) state: Arc<std::sync::Mutex<AppState>>,
    pub(crate) actions: Arc<Actions>,
    pub(crate) new_note_title: String,
    pub(crate) new_folder_name: String,
}

impl DesktopApp {
    pub fn new(state: Arc<std::sync::Mutex<AppState>>, actions: Arc<Actions>) -> Self {
        Self {
            state,
            actions,
            new_note_title: String::new(),
            new_folder_name: String::new(),
        }
    }
}

impl eframe::App for DesktopApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        // Initial refresh keeps panes consistent on first frame.
        {
            let mut state = self.state.lock().expect("state lock");
            if state.folders.is_empty() && state.notes.is_empty() {
                let _ = self.actions.refresh(&mut state);
            }
        }

        folder_sidebar::folder_sidebar(ui, self);
        note_list::top_search(ui, self);
        note_list::note_list_panel(ui, self);
        note_editor::editor_panel(ui, self);
        error_banner::error_banner(ui, self);
        error_banner::confirm_dialogs(ui, self);
    }
}

/// Confirms a pending deletion through the matching use case (shared by the
/// confirm dialog's Delete button).
pub(crate) fn execute_confirmed_delete(app: &mut DesktopApp, confirm: &PendingConfirm) {
    let mut state = app.state.lock().expect("state lock");
    let res = match confirm {
        PendingConfirm::DeleteNote(id) => app.actions.delete_note_confirmed(&mut state, id),
        PendingConfirm::DeleteFolder(id) => app.actions.delete_folder_confirmed(&mut state, id),
    };
    state.confirm_cancel();
    if let Err(e) = res {
        state.set_error(e);
    }
}
