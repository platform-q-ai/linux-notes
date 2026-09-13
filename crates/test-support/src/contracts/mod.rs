//! Shared behavioral contract suites for [`NoteRepository`] and
//! [`SearchService`] implementations. Test-only: see `lib.rs`.
//!
//! Each adapter adds one tiny integration test target that invokes
//! [`run_all`] / [`run_all_search`] with its own factories. That proves
//! substitutability (LSP) with behavioral tests instead of inheritance: the
//! in-memory fake and the real SQLite adapter must pass the very same cases.
//!
//! Fresh state per case: every case builds its own repository via the factory,
//! so cases never share mutable fixtures. "Reopen" (close and re-open the same
//! backend) is adapter-specific — the SQLite adapter proves it in its
//! integration tests by re-opening the same temp-dir database file; the
//! in-memory fake proves shared-store behavior via its `Clone`.
//!
//! [`NoteRepository`]: rusty_notes_application::ports::NoteRepository
//! [`SearchService`]: rusty_notes_application::ports::SearchService

pub mod repository_contract;
pub mod search_contract;

pub use repository_contract::run_all;
pub use search_contract::run_all_search;
