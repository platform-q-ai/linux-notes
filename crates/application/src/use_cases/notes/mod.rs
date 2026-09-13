//! Note use cases: the application policies over the note aggregate. One
//! snake_case module per use case (issue #2, F3).

mod create_note;
mod delete_note;
mod list_notes;
mod move_note;
mod open_note;
mod search_notes;
mod update_note;

pub use create_note::CreateNote;
pub use delete_note::DeleteNote;
pub use list_notes::ListNotes;
pub use move_note::MoveNote;
pub use open_note::OpenNote;
pub use search_notes::SearchNotes;
pub use update_note::UpdateNote;
