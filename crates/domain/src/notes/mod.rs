//! Note entity and its scoping value types. The note family lives together so
//! note-shape rules are discoverable in one place; invariants stay in `note.rs`.

mod note;
mod note_draft;
mod note_id;

pub use note::{Note, MAX_BODY_BYTES};
pub use note_draft::NoteDraft;
pub use note_id::NoteId;
