//! Error banner (thread 5: interactively dismissible) and the delete-confirm
//! modal.
//!
//! Thread 5 regression guard: the banner is one interactive row — the message
//! plus an explicit ✕ dismiss affordance — sized to its contents, never a
//! zero-size response target next to a passive label.
//!
//! Lock discipline: state locks are taken in statement-scoped blocks and clones
//! are used inside widget closures, so no `MutexGuard` can ever be held across a
//! second lock attempt (the classic egui `if let` scrutinee deadlock).

use super::DesktopApp;

pub(crate) fn error_banner(ui: &mut egui::Ui, app: &mut DesktopApp) {
    // Statement-scoped lock: the guard is gone before any widget closure runs.
    let error = {
        let state = app.state.lock().expect("state lock");
        state.error.clone()
    };
    if let Some(err) = error {
        egui::Grid::new("error_banner")
            .num_columns(2)
            .show(ui, |ui| {
                ui.colored_label(egui::Color32::RED, &err);
                if ui.small_button("✕").clicked() {
                    let mut state = app.state.lock().expect("state lock");
                    state.clear_error();
                }
            });
        ui.separator();
    }
}

pub(crate) fn confirm_dialogs(ui: &mut egui::Ui, app: &mut DesktopApp) {
    // Scope the state lock so it cannot overlap the modal closure's use of `app`.
    let confirm = {
        let state = app.state.lock().expect("state lock");
        state.pending_confirm.clone()
    };
    let Some(confirm) = confirm else {
        return;
    };
    let what = match confirm {
        crate::app::PendingConfirm::DeleteNote(_) => "note",
        crate::app::PendingConfirm::DeleteFolder(_) => "folder",
    };
    egui::containers::Modal::new(egui::Id::new("confirm-delete")).show(ui.ctx(), |ui| {
        ui.heading(format!("Delete {what}?"));
        ui.label("This cannot be undone.");
        ui.horizontal(|ui| {
            if ui.button("Cancel").clicked() {
                app.state.lock().expect("state lock").confirm_cancel();
            }
            if ui
                .button(egui::RichText::new("Delete").color(egui::Color32::RED))
                .clicked()
            {
                super::execute_confirmed_delete(app, &confirm);
            }
        });
    });
}
