//! Top search bar and middle pane: the note list (folder listing or search
//! results) plus note creation.
//!
//! Thread 1: clicking a row calls [`crate::app::Actions::open_note`], which loads
//! the FULL note through the `OpenNote` use case — the list row only carries
//! display data (title/preview) and must never populate the editor.
//!
//! Lock discipline: state locks are taken in statement-scoped blocks; clones are
//! used inside widget closures, so no `MutexGuard` is held across another lock
//! attempt or across a use-case call (the classic egui `if let` deadlock).

use super::DesktopApp;

pub(crate) fn top_search(ui: &mut egui::Ui, app: &mut DesktopApp) {
    egui::containers::Panel::top(egui::Id::new("search")).show(ui, |ui| {
        ui.horizontal(|ui| {
            let mut query = {
                let state = app.state.lock().expect("state lock");
                state.search_query.clone()
            };
            let resp = ui.add(egui::TextEdit::singleline(&mut query).hint_text("Search…"));
            if resp.changed() {
                app.state.lock().expect("state lock").begin_search(&query);
            }
            if ui.button("Search").clicked() {
                let mut state = app.state.lock().expect("state lock");
                let _ = app.actions.run_search(&mut state);
            }
        });
    });
}

pub(crate) fn note_list_panel(ui: &mut egui::Ui, app: &mut DesktopApp) {
    egui::containers::Panel::left(egui::Id::new("notes"))
        .resizable(true)
        .default_size(220.0)
        .show(ui, |ui| {
            // Middle pane: note list (folder listing or search results).
            egui::ScrollArea::vertical().show(ui, |ui| {
                // Snapshot under a short lock; no guard is held while widgets run.
                let notes = {
                    let state = app.state.lock().expect("state lock");
                    state.notes.clone()
                };
                let folders: Vec<(String, String)> = {
                    let state = app.state.lock().expect("state lock");
                    state
                        .folders
                        .iter()
                        .map(|f| (f.id.clone(), f.name.clone()))
                        .collect()
                };
                for item in notes {
                    ui.horizontal(|ui| {
                        if ui.selectable_label(item.selected, &item.title).clicked() {
                            let mut state = app.state.lock().expect("state lock");
                            if let Err(e) = app.actions.open_note(&mut state, &item.id) {
                                state.set_error(e);
                            }
                        }
                        // Move affordance: put the note into another folder.
                        ui.menu_button("→", |ui| {
                            for (fid, fname) in &folders {
                                if *fid == item.folder_id {
                                    continue; // already there
                                }
                                if ui.small_button(fname).clicked() {
                                    let mut state = app.state.lock().expect("state lock");
                                    if let Err(e) = app.actions.move_note(&mut state, &item.id, fid)
                                    {
                                        state.set_error(e);
                                    }
                                }
                            }
                        });
                    });
                }
            });
            ui.separator();
            ui.horizontal(|ui| {
                ui.text_edit_singleline(&mut app.new_note_title);
                if ui.button("New note").clicked() && !app.new_note_title.trim().is_empty() {
                    let mut state = app.state.lock().expect("state lock");
                    let title = app.new_note_title.trim().to_string();
                    match app.actions.create_note(&mut state, &title, "") {
                        Ok(()) => app.new_note_title.clear(),
                        Err(e) => state.set_error(e),
                    }
                }
            });
        });
}
