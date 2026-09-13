//! rusty-notes desktop — the ONLY crate that renders (eframe/egui) and the only
//! place that may depend on every lower layer.
//!
//! Layout (F7):
//! - [`composition`] — the composition root: builds the real production adapters
//!   (SQLite repository/search, system clock, self-seeding unique ids) and wires
//!   them into the use-case bundle. No test fakes ever appear here (thread 2/F8).
//! - [`app`] — framework-free shell logic: the [`app::AppState`] state machine and
//!   the [`app::Actions`] use-case bundle. Unit-tested headless, no egui.
//! - [`views`] — thin render callbacks: each pane is a function from `&egui::Ui`
//!   plus the state it renders. Rendering owns no state.
//!
//! `eframe::run_native` appears exactly once, in `main.rs`.

pub mod app;
pub mod composition;
pub mod views;
