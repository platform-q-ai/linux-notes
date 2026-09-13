//! rusty-notes application layer: ports and use cases.
//!
//! Depends only on the domain crate (dependency arrows point inward). No rusqlite,
//! no egui, no async runtime.
//!
//! This root module is a map, not an implementation (issue #2, F5):
//! - `ports/` — the narrow traits every adapter implements (`note_repository`,
//!   `search_service`, `clock`, `id_generator`), one port per file.
//! - `use_cases/` — one snake_case module per use case under `notes/` and
//!   `folders/`.
//! - `error.rs` — the errors crossing the application boundary.
//!
//! Test fakes and the shared behavioral contract suites live in the separate
//! `test-support` crate (dev-dependencies only) — never here (F8/F12).

pub mod error;
pub mod ports;
pub mod use_cases;

pub use error::{AppError, RepoError, SearchError};
pub use ports::{Clock, Ids, NoteRepository, SearchService};
