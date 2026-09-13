//! Folder use cases: create, rename, delete, list. One snake_case module per
//! use case (issue #2, F3).

mod create_folder;
mod delete_folder;
mod list_folders;
mod rename_folder;

pub use create_folder::CreateFolder;
pub use delete_folder::DeleteFolder;
pub use list_folders::ListFolders;
pub use rename_folder::RenameFolder;
