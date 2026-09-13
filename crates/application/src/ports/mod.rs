//! Ports: narrow traits owned by the application layer, implemented by adapters
//! outside it (hexagonal dependency rule — adapters depend inward on these).
//!
//! One port per file: `note_repository`, `search_service`, `clock`, `id_generator`.

pub mod clock;
pub mod id_generator;
pub mod note_repository;
pub mod search_service;

pub use clock::Clock;
pub use id_generator::Ids;
pub use note_repository::NoteRepository;
pub use search_service::SearchService;
