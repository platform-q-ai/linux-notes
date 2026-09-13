//! The `Folder` entity: business invariants of a container of notes.

use crate::error::DomainError;
use crate::FolderId;
use crate::Timestamp;

/// A folder. Folders are flat in v1 (no nesting) — see ADR-0004.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Folder {
    pub id: FolderId,
    pub name: String,
    pub created_at: Timestamp,
    pub updated_at: Timestamp,
}

impl Folder {
    /// Creates a folder, enforcing the non-empty name invariant.
    pub fn new(name: String, id: FolderId, now: Timestamp) -> Result<Self, DomainError> {
        let name = name.trim().to_string();
        if name.is_empty() {
            return Err(DomainError::EmptyFolderName);
        }
        Ok(Self {
            id,
            name,
            created_at: now.clone(),
            updated_at: now,
        })
    }

    /// Renames the folder; the name invariant is re-checked.
    pub fn rename(&mut self, name: String, now: Timestamp) -> Result<(), DomainError> {
        let name = name.trim().to_string();
        if name.is_empty() {
            return Err(DomainError::EmptyFolderName);
        }
        self.name = name;
        self.updated_at = now;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ts(value: &str) -> Timestamp {
        Timestamp(value.to_string())
    }

    #[test]
    fn folder_invariants() {
        let mut folder = Folder::new(
            " Inbox ".into(),
            FolderId("f1".into()),
            ts("2026-09-13T12:00:00Z"),
        )
        .expect("valid folder");
        assert_eq!(folder.name, "Inbox");

        let err = Folder::new(
            String::new(),
            FolderId("f2".into()),
            ts("2026-09-13T12:00:00Z"),
        )
        .expect_err("empty folder name must be rejected");
        assert_eq!(err, DomainError::EmptyFolderName);

        let err = folder
            .rename("   ".into(), ts("2026-09-13T13:00:00Z"))
            .expect_err("empty folder name must be rejected");
        assert_eq!(err, DomainError::EmptyFolderName);
        assert_eq!(folder.updated_at, ts("2026-09-13T12:00:00Z"));

        folder
            .rename("Archive".into(), ts("2026-09-13T13:00:00Z"))
            .expect("valid rename");
        assert_eq!(folder.name, "Archive");
        assert_eq!(folder.updated_at, ts("2026-09-13T13:00:00Z"));
    }
}
