//! Folder entity and its identifier. Folders are flat in v1 (no nesting) — see ADR-0004.

mod folder;
mod folder_id;

pub use folder::Folder;
pub use folder_id::FolderId;
