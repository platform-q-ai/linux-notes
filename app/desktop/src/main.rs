//! rusty-notes desktop — composition root and the ONLY place eframe/egui appears.
//!
//! Wiring: SQLite adapter (file db) -> use cases -> framework-free UI state
//! (`ui::AppState`) rendered as three panes (folders | notes + search | editor).
//! `eframe::run_native` is never executed by tests; all UI logic lives in
//! `ui::AppState` (unit-tested headless) and thin render callbacks here.

mod ui;

use std::sync::Arc;

use rusty_notes_application::memory::SequentialIds;
use rusty_notes_application::ports::{Clock, Ids, NoteRepository, SearchService};
use rusty_notes_application::use_cases::{
    CreateFolder, CreateNote, DeleteFolder, DeleteNote, ListFolders, ListNotes, MoveNote,
    SearchNotes, UpdateNote,
};
use rusty_notes_sqlite::{SqliteNoteRepository, SqliteSearchService};
use ui::{Actions, AppState};

/// Database location: `rusty-notes.db` in the current directory (local-first).
const DB_FILE: &str = "rusty-notes.db";

/// Builds the concrete wiring: ONE SQLite connection shared by repository and
/// search, wrapped into the use cases via `Arc<dyn Port>`.
fn build_wired(db_path: &str) -> Result<Actions, String> {
    let conn = rusty_notes_sqlite::open(db_path).map_err(|e| e.to_string())?;
    let conn = Arc::new(std::sync::Mutex::new(conn));
    let repo: Arc<dyn NoteRepository> = Arc::new(SqliteNoteRepository::shared(conn.clone()));
    let search: Arc<dyn SearchService> = Arc::new(SqliteSearchService::shared(conn));
    let clock: Arc<dyn Clock> = Arc::new(SystemClock);
    let ids: Arc<dyn Ids> = Arc::new(SequentialIds::default());
    Ok(Actions {
        list_notes: ListNotes::new(repo.clone()),
        list_folders: ListFolders::new(repo.clone()),
        create_note: CreateNote::new(repo.clone(), ids.clone(), clock.clone()),
        update_note: UpdateNote::new(repo.clone(), clock.clone()),
        delete_note: DeleteNote::new(repo.clone()),
        move_note: MoveNote::new(repo.clone(), clock.clone()),
        create_folder: CreateFolder::new(repo.clone(), ids, clock.clone()),
        delete_folder: DeleteFolder::new(repo.clone()),
        search: SearchNotes::new(search, repo.clone()),
    })
}

/// Wall-clock implementation of the Clock port for the running app.
struct SystemClock;

impl Clock for SystemClock {
    fn now(&self) -> rusty_notes_domain::Timestamp {
        // Seconds-precision UTC RFC-3339 without external time crates: SQLite and
        // the contract only need a stable, lexicographically ordered format.
        let secs = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        rusty_notes_domain::Timestamp(rfc3339_from_unix(secs))
    }
}

/// Formats unix seconds as `YYYY-MM-DDTHH:MM:SSZ` (proleptic Gregorian, UTC).
fn rfc3339_from_unix(secs: u64) -> String {
    let days = secs / 86_400;
    let rem = secs % 86_400;
    let (h, m, s) = (rem / 3600, (rem % 3600) / 60, rem % 60);
    // Civil-from-days algorithm (Howard Hinnant), valid for the full u64 range.
    let z = days as i64 + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let mth = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if mth <= 2 { y + 1 } else { y };
    format!("{y:04}-{mth:02}-{d:02}T{h:02}:{m:02}:{s:02}Z")
}

fn main() {
    let actions = build_wired(DB_FILE).unwrap_or_else(|e| {
        eprintln!("fatal: cannot open database ({e})");
        std::process::exit(1);
    });
    let options = eframe::NativeOptions::default();
    if let Err(e) = run_app(AppState::default(), actions, options) {
        eprintln!("fatal: {e}");
        std::process::exit(1);
    }
}

fn run_app(
    state: AppState,
    actions: Actions,
    options: eframe::NativeOptions,
) -> Result<(), eframe::Error> {
    let state = Arc::new(std::sync::Mutex::new(state));
    let actions = Arc::new(actions);
    eframe::run_native(
        "rusty-notes",
        options,
        Box::new(move |_cc| {
            Ok(Box::new(App {
                state,
                actions,
                new_note_title: String::new(),
                new_folder_name: String::new(),
            }))
        }),
    )
}

struct App {
    state: Arc<std::sync::Mutex<AppState>>,
    actions: Arc<Actions>,
    new_note_title: String,
    new_folder_name: String,
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        // Initial refresh keeps panes consistent on first frame.
        {
            let mut state = self.state.lock().expect("state lock");
            if state.folders.is_empty() && state.notes.is_empty() {
                let _ = self.actions.refresh(&mut state);
            }
        }

        egui::containers::Panel::left(egui::Id::new("folders")).show(ui, |ui| {
            ui.heading("Folders");
            let mut select: Option<Option<String>> = None;
            let currently_all = self.state.lock().unwrap().selected_folder.is_none();
            if ui.selectable_label(currently_all, "All notes").clicked() {
                select = Some(None);
            }
            let rows = self.state.lock().unwrap().folders.clone();
            for (id, name, count) in rows {
                let selected =
                    self.state.lock().unwrap().selected_folder.as_deref() == Some(id.as_str());
                ui.horizontal(|ui| {
                    if ui
                        .selectable_label(selected, format!("{name} ({count})"))
                        .clicked()
                    {
                        select = Some(Some(id.clone()));
                    }
                    if ui.small_button("✕").clicked() {
                        self.state.lock().unwrap().request_delete_folder(&id);
                    }
                });
            }
            if let Some(sel) = select {
                {
                    let mut state = self.state.lock().unwrap();
                    state.select_folder(sel);
                    let _ = self.actions.refresh(&mut state);
                }
            }
            ui.separator();
            ui.horizontal(|ui| {
                ui.text_edit_singleline(&mut self.new_folder_name);
                if ui.button("New folder").clicked() && !self.new_folder_name.trim().is_empty() {
                    let mut state = self.state.lock().unwrap();
                    let name = self.new_folder_name.trim().to_string();
                    match self
                        .actions
                        .create_folder
                        .execute(name)
                        .map_err(|e| e.to_string())
                    {
                        Ok(_f) => {
                            self.new_folder_name.clear();
                            let _ = self.actions.refresh(&mut state);
                        }
                        Err(e) => state.set_error(e),
                    }
                }
            });
        });

        egui::containers::Panel::top(egui::Id::new("search")).show(ui, |ui| {
            ui.horizontal(|ui| {
                let mut query = self.state.lock().unwrap().search_query.clone();
                let resp = ui.add(egui::TextEdit::singleline(&mut query).hint_text("Search…"));
                if resp.changed() {
                    self.state.lock().unwrap().begin_search(&query);
                }
                if ui.button("Search").clicked() {
                    let mut state = self.state.lock().unwrap();
                    let _ = self.actions.run_search(&mut state);
                }
            });
        });

        egui::containers::Panel::left(egui::Id::new("notes"))
            .resizable(true)
            .default_size(220.0)
            .show(ui, |ui| {
                // Middle pane: note list (folder listing or search results).
                egui::ScrollArea::vertical().show(ui, |ui| {
                    let notes = self.state.lock().unwrap().notes.clone();
                    let folders = self.state.lock().unwrap().folders.clone();
                    for item in notes {
                        let label = format!("{} — {}", item.title, item.preview);
                        ui.horizontal(|ui| {
                            if ui.selectable_label(item.selected, &label).clicked() {
                                self.state.lock().unwrap().select_note(&item.id);
                            }
                            // Move affordance: drop the note into another folder.
                            ui.menu_button("→", |ui| {
                                for (fid, fname, _count) in &folders {
                                    if fid == &item.folder_id {
                                        continue; // already there
                                    }
                                    if ui.small_button(fname).clicked() {
                                        let mut state = self.state.lock().unwrap();
                                        if let Err(e) =
                                            self.actions.move_note(&mut state, &item.id, fid)
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
                    ui.text_edit_singleline(&mut self.new_note_title);
                    if ui.button("New note").clicked() && !self.new_note_title.trim().is_empty() {
                        let mut state = self.state.lock().unwrap();
                        let title = self.new_note_title.trim().to_string();
                        match self.actions.create_note(&mut state, &title, "") {
                            Ok(()) => {
                                self.new_note_title.clear();
                            }
                            Err(e) => state.set_error(e),
                        }
                    }
                });
            });

        egui::containers::Panel::bottom(egui::Id::new("editor"))
            .resizable(true)
            .default_size(240.0)
            .show(ui, |ui| {
                let (dirty, open, title, body) = {
                    let state = self.state.lock().unwrap();
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
                        let mut state = self.state.lock().unwrap();
                        if let Err(e) = self.actions.save(&mut state) {
                            state.set_error(e);
                        }
                    }
                    if ui
                        .add_enabled(open.is_some(), egui::Button::new("Delete"))
                        .clicked()
                    {
                        let mut state = self.state.lock().unwrap();
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
                    if title != self.state.lock().unwrap().editor.title {
                        self.state.lock().unwrap().edit_title(title);
                    }
                    egui::TextEdit::multiline(&mut body)
                        .hint_text("Take a note…")
                        .desired_rows(8)
                        .show(ui);
                    if body != self.state.lock().unwrap().editor.body {
                        self.state.lock().unwrap().edit_body(body);
                    }
                } else {
                    ui.label("Select a note, or create one below.");
                }
            });

        // Error banner.
        if let Some(err) = self.state.lock().unwrap().error.clone() {
            ui.colored_label(egui::Color32::RED, format!("{err} — click to dismiss"));
            if ui
                .allocate_response(egui::Vec2::ZERO, egui::Sense::click())
                .clicked()
            {
                self.state.lock().unwrap().clear_error();
            }
        }

        // Confirm dialogs.
        if let Some(confirm) = self.state.lock().unwrap().pending_confirm.clone() {
            let (what, id) = match confirm {
                ui::PendingConfirm::DeleteNote(id) => ("note", id),
                ui::PendingConfirm::DeleteFolder(id) => ("folder", id),
            };
            egui::containers::Modal::new(egui::Id::new("confirm-delete")).show(ui.ctx(), |ui| {
                ui.heading(format!("Delete {what}?"));
                ui.label("This cannot be undone.");
                ui.horizontal(|ui| {
                    if ui.button("Cancel").clicked() {
                        self.state.lock().unwrap().confirm_cancel();
                    }
                    if ui
                        .button(egui::RichText::new("Delete").color(egui::Color32::RED))
                        .clicked()
                    {
                        let mut state = self.state.lock().unwrap();
                        let res = match what {
                            "note" => self.actions.delete_note_confirmed(&mut state, &id),
                            _ => self.actions.delete_folder_confirmed(&mut state, &id),
                        };
                        state.confirm_cancel();
                        if let Err(e) = res {
                            state.set_error(e);
                        }
                    }
                });
            });
        }
    }
}
