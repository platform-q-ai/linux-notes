//! Errors raised by domain invariants. Kept at the crate root because both the
//! notes and folders families (and every outer layer) must be able to name them.

use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DomainError {
    /// A note title must not be empty (after trimming).
    EmptyTitle,
    /// A folder name must not be empty (after trimming).
    EmptyFolderName,
    /// Note bodies are capped to keep list views and sync payloads sane.
    /// Enforced on every mutation path, not just creation.
    BodyTooLarge(usize),
}

impl fmt::Display for DomainError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DomainError::EmptyTitle => write!(f, "note title must not be empty"),
            DomainError::EmptyFolderName => write!(f, "folder name must not be empty"),
            DomainError::BodyTooLarge(max) => {
                write!(f, "note body exceeds the {max}-byte limit")
            }
        }
    }
}

impl std::error::Error for DomainError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn body_too_large_message_names_the_limit() {
        let err = DomainError::BodyTooLarge(1024 * 1024).to_string();
        assert_eq!(err, "note body exceeds the 1048576-byte limit");
    }
}
