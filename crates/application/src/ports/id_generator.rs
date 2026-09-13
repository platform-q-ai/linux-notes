//! Identifier-generation port.

use rusty_notes_domain::{FolderId, NoteId};

/// Identifier-generation port. Production implementation:
/// `rusty_notes_system::UniqueIds` (unique across restarts). Tests use
/// `test-support`'s `SequentialIds` (dev-dependency only); wiring that fake in
/// production caused the restart id-collision bug (behavior thread 2 / F8).
pub trait Ids: Send + Sync {
    fn next_note_id(&self) -> NoteId;
    fn next_folder_id(&self) -> FolderId;
}
