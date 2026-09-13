//! rusty-notes desktop binary: wires the production adapters (composition root)
//! and hands the state + use cases to the view shell. All UI logic lives in the
//! headless [`crate::app`] state machine (unit-tested) and the thin pane views
//! in [`crate::views`]; `eframe::run_native` appears exactly once, here.

use rusty_notes_desktop::{app::AppState, composition};

fn main() {
    let actions = composition::build_wired(composition::DB_FILE).unwrap_or_else(|e| {
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
    actions: rusty_notes_desktop::app::Actions,
    options: eframe::NativeOptions,
) -> Result<(), eframe::Error> {
    let state = std::sync::Arc::new(std::sync::Mutex::new(state));
    let actions = std::sync::Arc::new(actions);
    eframe::run_native(
        "rusty-notes",
        options,
        Box::new(move |_cc| {
            Ok(Box::new(rusty_notes_desktop::views::DesktopApp::new(
                state, actions,
            )))
        }),
    )
}
