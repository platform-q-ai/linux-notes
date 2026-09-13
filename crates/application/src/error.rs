//! Errors crossing the application boundary.

use std::fmt;

use rusty_notes_domain::DomainError;

/// Error returned by [`crate::ports::NoteRepository`] implementations.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RepoError {
    /// The requested note or folder does not exist (or is soft-deleted).
    NotFound,
    /// The folder still contains notes (including soft-deleted ones; they leave
    /// via `purge_note`, not `delete_folder`).
    FolderHasNotes,
    /// A uniqueness constraint was violated (e.g. duplicate id insert).
    Conflict,
    /// Opaque storage failure; adapters wrap their backend message here.
    Storage(String),
}

impl fmt::Display for RepoError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RepoError::NotFound => write!(f, "not found"),
            RepoError::FolderHasNotes => write!(f, "folder still contains notes"),
            RepoError::Conflict => write!(f, "conflict: record already exists"),
            RepoError::Storage(msg) => write!(f, "storage error: {msg}"),
        }
    }
}

impl std::error::Error for RepoError {}

/// Error returned by [`crate::ports::SearchService`] implementations.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SearchError {
    /// Blank queries are rejected instead of returning everything.
    EmptyQuery,
    /// Opaque search-backend failure.
    Storage(String),
}

impl fmt::Display for SearchError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SearchError::EmptyQuery => write!(f, "search query must not be empty"),
            SearchError::Storage(msg) => write!(f, "search error: {msg}"),
        }
    }
}

impl std::error::Error for SearchError {}

/// Error returned by use cases: domain invariant, port, or existence failures.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AppError {
    Domain(DomainError),
    Repo(RepoError),
    Search(SearchError),
    /// The use case needed a note that does not exist.
    NoteMissing,
    /// The use case needed a folder that does not exist.
    FolderMissing,
}

impl fmt::Display for AppError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppError::Domain(e) => write!(f, "{e}"),
            AppError::Repo(e) => write!(f, "{e}"),
            AppError::Search(e) => write!(f, "{e}"),
            AppError::NoteMissing => write!(f, "note does not exist"),
            AppError::FolderMissing => write!(f, "folder does not exist"),
        }
    }
}

impl std::error::Error for AppError {}

impl From<DomainError> for AppError {
    fn from(e: DomainError) -> Self {
        AppError::Domain(e)
    }
}

impl From<RepoError> for AppError {
    fn from(e: RepoError) -> Self {
        AppError::Repo(e)
    }
}

impl From<SearchError> for AppError {
    fn from(e: SearchError) -> Self {
        AppError::Search(e)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn app_error_display_and_conversion() {
        let e: AppError = DomainError::EmptyTitle.into();
        assert_eq!(e.to_string(), "note title must not be empty");
        let e: AppError = RepoError::FolderHasNotes.into();
        assert_eq!(e.to_string(), "folder still contains notes");
        let e: AppError = SearchError::EmptyQuery.into();
        assert_eq!(e.to_string(), "search query must not be empty");
        assert_eq!(AppError::NoteMissing.to_string(), "note does not exist");
        assert_eq!(AppError::FolderMissing.to_string(), "folder does not exist");
    }
}
