//! Left pane: folders (with live note counts), folder creation, deletion entry.

use super::DesktopApp;

pub(crate) fn folder_sidebar(ui: &mut egui::Ui, app: &mut DesktopApp) {
    egui::containers::Panel::left(egui::Id::new("folders")).show(ui, |ui| {
        ui.heading("Folders");
        let mut select: Option<Option<String>> = None;
        let currently_all = {
            let state = app.state.lock().expect("state lock");
            state.selected_folder.is_none()
        };
        if ui.selectable_label(currently_all, "All notes").clicked() {
            select = Some(None);
        }
        // Snapshot rows under a short lock; no guard is held while widgets run.
        let rows: Vec<(String, String, usize)> = {
            let state = app.state.lock().expect("state lock");
            state
                .folders
                .iter()
                .map(|f| (f.id.clone(), f.name.clone(), f.note_count))
                .collect()
        };
        for (id, name, count) in rows {
            let selected = {
                let state = app.state.lock().expect("state lock");
                state.selected_folder.as_deref() == Some(id.as_str())
            };
            ui.horizontal(|ui| {
                if ui
                    .selectable_label(selected, format!("{name} ({count})"))
                    .clicked()
                {
                    select = Some(Some(id.clone()));
                }
                if ui.small_button("✕").clicked() {
                    app.state
                        .lock()
                        .expect("state lock")
                        .request_delete_folder(&id);
                }
            });
        }
        if let Some(sel) = select {
            let mut state = app.state.lock().expect("state lock");
            state.select_folder(sel);
            let _ = app.actions.refresh(&mut state);
        }
        ui.separator();
        ui.horizontal(|ui| {
            ui.text_edit_singleline(&mut app.new_folder_name);
            if ui.button("New folder").clicked() && !app.new_folder_name.trim().is_empty() {
                let mut state = app.state.lock().expect("state lock");
                let name = app.new_folder_name.trim().to_string();
                match app
                    .actions
                    .create_folder
                    .execute(name)
                    .map_err(|e| e.to_string())
                {
                    Ok(_f) => {
                        app.new_folder_name.clear();
                        let _ = app.actions.refresh(&mut state);
                    }
                    Err(e) => state.set_error(e),
                }
            }
        });
    });
}
