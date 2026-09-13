//! Bottom pane: the editor. Renders the presentation crate's `EditorState`
//! (title + body + dirty flag) and commits through `Actions::save`.
//!
//! Lock discipline: the editor snapshot is cloned under one short lock; the
//! `changed` feedback writes take fresh short locks afterwards.

use super::DesktopApp;

pub(crate) fn editor_panel(ui: &mut egui::Ui, app: &mut DesktopApp) {
    egui::containers::Panel::bottom(egui::Id::new("editor"))
        .resizable(true)
        .default_size(240.0)
        .show(ui, |ui| {
            let (dirty, open, title, body) = {
                let state = app.state.lock().expect("state lock");
                (
                    state.editor.dirty,
                    state.editor.note_id.clone(),
                    state.editor.title.clone(),
                    state.editor.body.clone(),
                )
            };
            ui.horizontal(|ui| {
                ui.heading("Editor");
                if dirty {
                    ui.label("(unsaved)");
                }
                if ui
                    .add_enabled(open.is_some() && dirty, egui::Button::new("Save"))
                    .clicked()
                {
                    let mut state = app.state.lock().expect("state lock");
                    if let Err(e) = app.actions.save(&mut state) {
                        state.set_error(e);
                    }
                }
                if ui
                    .add_enabled(open.is_some(), egui::Button::new("Delete"))
                    .clicked()
                {
                    let mut state = app.state.lock().expect("state lock");
                    if let Some(id) = state.selected_note.clone() {
                        state.request_delete_note(&id);
                    }
                }
            });
            if open.is_some() {
                let mut title = title;
                let mut body = body;
                egui::TextEdit::singleline(&mut title)
                    .hint_text("Title")
                    .show(ui);
                if title != app.state.lock().expect("state lock").editor.title {
                    app.state.lock().expect("state lock").edit_title(title);
                }
                egui::TextEdit::multiline(&mut body)
                    .hint_text("Take a note…")
                    .desired_rows(8)
                    .show(ui);
                if body != app.state.lock().expect("state lock").editor.body {
                    app.state.lock().expect("state lock").edit_body(body);
                }
            } else {
                ui.label("Select a note, or create one below.");
            }
        });
}
