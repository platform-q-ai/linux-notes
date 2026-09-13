//! rusty-notes presentation adapter: framework-free view models for the desktop
//! shell.
//!
//! This is explicitly presentation, not application policy (issue #2): the types
//! here describe what the UI renders, depend only on the domain crate, and never
//! touch ports, use cases, storage, or egui. Mapping from domain types is kept
//! next to the view models so it is unit-tested headless with no framework
//! involved.

pub mod editor_state;
pub mod folder_list_item;
pub mod note_list_item;
pub mod notes_presenter;

pub use editor_state::EditorState;
pub use folder_list_item::FolderListItem;
pub use note_list_item::NoteListItem;
pub use notes_presenter::{folder_list_items, note_list_items};
