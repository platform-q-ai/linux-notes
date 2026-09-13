//! rusty-notes domain layer: entities, value objects, invariants.
//!
//! Pure Rust: no IO, no frameworks, no in-workspace dependencies (ADR: dependency
//! arrows point inward). Timestamps are UTC RFC-3339 strings supplied through the
//! `Clock` port in the application layer, so time is always an injected value here.
//!
//! This root module is a map, not an implementation: business logic lives in the
//! named modules (`notes/`, `folders/`, `shared/`, `error.rs`) and is re-exported
//! here so consumers can keep using the flat `rusty_notes_domain::` paths.

pub mod error;
pub mod folders;
pub mod notes;
pub mod shared;

pub use error::DomainError;
pub use folders::{Folder, FolderId};
pub use notes::{Note, NoteDraft, NoteId, MAX_BODY_BYTES};
pub use shared::Timestamp;
