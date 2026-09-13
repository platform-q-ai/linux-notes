//! Ports: narrow traits owned by the application layer, implemented by adapters
//! outside it (hexagonal dependency rule — adapters depend inward on these).

use rusty_notes_domain::{Folder, FolderId, Note, NoteId, Timestamp};

use crate::error::{RepoError, SearchError};

/// Persistence port for notes and folders.
///
/// Implementations: [`crate::memory::InMemoryNoteRepository`] (tests/fake) and
/// `rusty_notes_sqlite::SqliteNoteRepository` (real). Both must honor the shared
/// behavioral contract exercised by [`crate::contract`].
///
/// Ordering contract: `list_notes_by_folder` and `list_all_notes` return notes
/// ordered by `updated_at` descending (most recently touched first; equal
/// timestamps keep the later-created id first, i.e. id DESC);
/// `list_folders` returns folders ordered by `name` ascending.
/// Soft-delete semantics: deleted notes disappear from all listings and search
/// but remain recoverable by `get_note` until purged; `delete_folder` refuses
/// while any note — live or soft-deleted — references the folder.
pub trait NoteRepository: Send + Sync {
    fn insert_note(&self, note: &Note) -> Result<(), RepoError>;
    fn get_note(&self, id: &NoteId) -> Result<Option<Note>, RepoError>;
    /// Full overwrite of a live note; `RepoError::NotFound` if it does not exist.
    fn update_note(&self, note: &Note) -> Result<(), RepoError>;
    /// Soft delete. Missing notes are `RepoError::NotFound` (deleting is explicit,
    /// so callers learn they deleted nothing).
    fn soft_delete_note(&self, id: &NoteId) -> Result<(), RepoError>;
    fn list_notes_by_folder(&self, folder_id: &FolderId) -> Result<Vec<Note>, RepoError>;
    fn list_all_notes(&self) -> Result<Vec<Note>, RepoError>;
    fn insert_folder(&self, folder: &Folder) -> Result<(), RepoError>;
    fn get_folder(&self, id: &FolderId) -> Result<Option<Folder>, RepoError>;
    fn list_folders(&self) -> Result<Vec<Folder>, RepoError>;
    /// Full overwrite of a folder; `RepoError::NotFound` if it does not exist.
    fn update_folder(&self, folder: &Folder) -> Result<(), RepoError>;
    /// Fails with `RepoError::FolderHasNotes` while any note (live or soft-deleted)
    /// references the folder.
    fn delete_folder(&self, id: &FolderId) -> Result<(), RepoError>;
}

/// Search port. Returns matching note ids, best match first, excluding soft-deleted
/// notes. Blank queries yield [`SearchError::EmptyQuery`].
pub trait SearchService: Send + Sync {
    fn search(&self, query: &str) -> Result<Vec<NoteId>, SearchError>;
}

/// Time port: use cases take timestamps as injected values, keeping the domain
/// deterministic and testable.
pub trait Clock: Send + Sync {
    fn now(&self) -> Timestamp;
}

/// Identifier-generation port.
pub trait Ids: Send + Sync {
    fn next_note_id(&self) -> NoteId;
    fn next_folder_id(&self) -> FolderId;
}
