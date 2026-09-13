//! Persistence port for notes and folders.

use rusty_notes_domain::{Folder, FolderId, Note, NoteId};

use crate::error::RepoError;

/// Persistence port for notes and folders.
///
/// Implementations: the in-memory fake (`test-support`'s
/// `InMemoryNoteRepository`, dev-dependency only) and
/// `rusty_notes_sqlite::SqliteNoteRepository` (real). Both must honor the shared
/// behavioral contract exercised by `test-support`'s contract suites in their
/// integration tests.
///
/// Ordering contract: `list_notes_by_folder` and `list_all_notes` return notes
/// ordered by `updated_at` descending (most recently touched first; equal
/// timestamps keep the later-created id first, i.e. id DESC);
/// `list_folders` returns folders ordered by `name` ascending.
///
/// Soft-delete lifecycle (behavior thread 6): deleted notes disappear from all
/// listings and search AND from [`NoteRepository::get_note`] — the plain reads
/// expose live rows only. The deleted row stays explicitly reachable through
/// [`NoteRepository::get_note_including_deleted`] until it is removed for good
/// by [`NoteRepository::purge_note`] ("empty trash"). `delete_folder` refuses
/// while any note — live or soft-deleted — references the folder; purging the
/// deleted notes makes the folder deletable again.
pub trait NoteRepository: Send + Sync {
    fn insert_note(&self, note: &Note) -> Result<(), RepoError>;
    /// Live notes only: a soft-deleted note reads as `None` here; use
    /// [`NoteRepository::get_note_including_deleted`] to recover it.
    fn get_note(&self, id: &NoteId) -> Result<Option<Note>, RepoError>;
    /// Full overwrite of a live note; `RepoError::NotFound` if it does not exist
    /// (or is soft-deleted). A missing target folder is also `RepoError::NotFound`
    /// on every adapter — never an opaque storage error (behavior thread 7).
    fn update_note(&self, note: &Note) -> Result<(), RepoError>;
    /// Soft delete. Missing notes are `RepoError::NotFound` (deleting is explicit,
    /// so callers learn they deleted nothing).
    fn soft_delete_note(&self, id: &NoteId) -> Result<(), RepoError>;
    /// Explicitly reads a note whether it is live or soft-deleted: this is the
    /// recovery surface promised for soft-deleted rows (behavior thread 6).
    /// Returns `Ok(None)` only when no row with this id exists at all.
    fn get_note_including_deleted(&self, id: &NoteId) -> Result<Option<Note>, RepoError>;
    /// Permanently removes a soft-deleted note ("empty trash"). Only soft-deleted
    /// notes can be purged; purging a live note or a missing id is
    /// `RepoError::NotFound`. After a purge the row is gone through the whole
    /// port surface, and the folder it referenced becomes deletable.
    fn purge_note(&self, id: &NoteId) -> Result<(), RepoError>;
    fn list_notes_by_folder(&self, folder_id: &FolderId) -> Result<Vec<Note>, RepoError>;
    fn list_all_notes(&self) -> Result<Vec<Note>, RepoError>;
    fn insert_folder(&self, folder: &Folder) -> Result<(), RepoError>;
    fn get_folder(&self, id: &FolderId) -> Result<Option<Folder>, RepoError>;
    fn list_folders(&self) -> Result<Vec<Folder>, RepoError>;
    /// Full overwrite of a folder; `RepoError::NotFound` if it does not exist.
    fn update_folder(&self, folder: &Folder) -> Result<(), RepoError>;
    /// Fails with `RepoError::FolderHasNotes` while any note (live or soft-deleted)
    /// references the folder; purge the deleted notes to clear the folder.
    fn delete_folder(&self, id: &FolderId) -> Result<(), RepoError>;
}
