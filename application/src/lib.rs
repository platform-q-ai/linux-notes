//! rusty-notes application layer: ports, use cases, in-memory adapters,
//! shared contract suite, presenter view models.
//!
//! Depends only on the domain crate (dependency arrows point inward). No rusqlite,
//! no egui, no async runtime.

pub mod contract;
pub mod error;
pub mod memory;
pub mod ports;
pub mod presenter;
pub mod use_cases;

pub use error::{AppError, RepoError, SearchError};
pub use ports::{Clock, Ids, NoteRepository, SearchService};
