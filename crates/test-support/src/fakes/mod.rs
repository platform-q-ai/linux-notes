//! In-memory fakes for the application ports. Test-only: see `lib.rs`.

pub mod fixed_clock;
pub mod in_memory_note_repository;
pub mod in_memory_search;
pub mod sequential_ids;

pub use fixed_clock::FixedClock;
pub use in_memory_note_repository::InMemoryNoteRepository;
pub use in_memory_search::InMemorySearch;
pub use sequential_ids::SequentialIds;
