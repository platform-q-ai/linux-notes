//! SQLite adapter: migrations, repository, and FTS5 search over the application
//! ports. This is the only crate that talks to rusqlite; everything above it sees
//! the narrow `NoteRepository` / `SearchService` traits.
//!
//! Module map (F4/F5 — named modules instead of a monolith): `connection.rs`
//! (physical connections + FTS5 canary), `migrations.rs` (versioned schema),
//! `note_repository.rs` / `search_service.rs` (port implementations),
//! `error_mapping.rs` (rusqlite error translation).

pub mod connection;
pub mod error_mapping;
pub mod migrations;
pub mod note_repository;
pub mod search_service;

pub use connection::{open, open_in_memory, FtsUnavailable, OpenError};
pub use migrations::{migrate, SCHEMA_VERSION};
pub use note_repository::SqliteNoteRepository;
pub use search_service::SqliteSearchService;
