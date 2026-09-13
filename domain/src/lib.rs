//! rusty-notes domain layer: entities, value objects, invariants.
//!
//! Pure Rust: no IO, no frameworks, no in-workspace dependencies (ADR: dependency
//! arrows point inward). Timestamps are UTC RFC-3339 strings supplied through the
//! `Clock` port in the application layer, so time is always an injected value here.

use std::fmt;

/// Unique identifier of a note. Newtype so IDs cannot be confused with other strings.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct NoteId(pub String);

impl NoteId {
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for NoteId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

/// Unique identifier of a folder.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct FolderId(pub String);

impl FolderId {
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for FolderId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

/// Point in time as a UTC RFC-3339 string. Lexicographic order equals chronological
/// order for the fixed format produced by the application layer's clock port.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Timestamp(pub String);

impl Timestamp {
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for Timestamp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

/// Errors raised by domain invariants.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DomainError {
    /// A note title must not be empty (after trimming).
    EmptyTitle,
    /// A folder name must not be empty (after trimming).
    EmptyFolderName,
    /// Note bodies are capped to keep list views and sync payloads sane.
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

/// Data required to create a new note.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NoteDraft {
    pub folder_id: FolderId,
    pub title: String,
    pub body: String,
}

/// Hard cap on note body size (1 MiB). Small enough to keep UI list views and
/// any later sync payload sane, large enough that no real note hits it.
pub const MAX_BODY_BYTES: usize = 1024 * 1024;

/// A note. `updated_at` only changes through explicit operations (rename, edit_body,
/// move_to) — never implicitly.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Note {
    pub id: NoteId,
    pub folder_id: FolderId,
    pub title: String,
    pub body: String,
    pub created_at: Timestamp,
    pub updated_at: Timestamp,
}

impl Note {
    /// Creates a note, enforcing the non-empty title invariant.
    pub fn new(draft: NoteDraft, id: NoteId, now: Timestamp) -> Result<Self, DomainError> {
        let title = draft.title.trim().to_string();
        if title.is_empty() {
            return Err(DomainError::EmptyTitle);
        }
        if draft.body.len() > MAX_BODY_BYTES {
            return Err(DomainError::BodyTooLarge(MAX_BODY_BYTES));
        }
        Ok(Self {
            id,
            folder_id: draft.folder_id,
            title,
            body: draft.body,
            created_at: now.clone(),
            updated_at: now,
        })
    }

    /// Renames the note; the title invariant is re-checked.
    pub fn rename(&mut self, title: String, now: Timestamp) -> Result<(), DomainError> {
        let title = title.trim().to_string();
        if title.is_empty() {
            return Err(DomainError::EmptyTitle);
        }
        self.title = title;
        self.updated_at = now;
        Ok(())
    }

    /// Replaces the body. Bodies may be empty.
    pub fn edit_body(&mut self, body: String, now: Timestamp) {
        self.body = body;
        self.updated_at = now;
    }

    /// Moves the note into another folder.
    pub fn move_to(&mut self, folder: FolderId, now: Timestamp) {
        self.folder_id = folder;
        self.updated_at = now;
    }

    /// True if the note has been soft-deleted (kept for adapters that carry the flag).
    /// The domain model itself has no deleted flag: soft deletion is a persistence
    /// concern behind the `NoteRepository` port.
    pub fn is_live(&self) -> bool {
        true
    }
}

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
    fn create_note_trims_title_and_stamps_timestamps() {
        let note = Note::new(
            NoteDraft {
                folder_id: FolderId("f1".into()),
                title: "  shopping  ".into(),
                body: "milk".into(),
            },
            NoteId("n1".into()),
            ts("2026-09-13T12:00:00Z"),
        )
        .expect("valid note");
        assert_eq!(note.title, "shopping");
        assert_eq!(note.created_at, ts("2026-09-13T12:00:00Z"));
        assert_eq!(note.updated_at, note.created_at);
    }

    #[test]
    fn create_note_rejects_empty_title() {
        let err = Note::new(
            NoteDraft {
                folder_id: FolderId("f1".into()),
                title: "   ".into(),
                body: String::new(),
            },
            NoteId("n1".into()),
            ts("2026-09-13T12:00:00Z"),
        )
        .expect_err("empty title must be rejected");
        assert_eq!(err, DomainError::EmptyTitle);
    }

    #[test]
    fn rename_updates_title_and_bumps_updated_at() {
        let mut note = valid_note();
        note.rename("renamed".into(), ts("2026-09-13T13:00:00Z"))
            .expect("valid rename");
        assert_eq!(note.title, "renamed");
        assert_eq!(note.updated_at, ts("2026-09-13T13:00:00Z"));
        assert_eq!(note.created_at, ts("2026-09-13T12:00:00Z"));
    }

    #[test]
    fn rename_rejects_empty_title_without_touching_timestamp() {
        let mut note = valid_note();
        let err = note
            .rename("  ".into(), ts("2026-09-13T13:00:00Z"))
            .expect_err("empty title must be rejected");
        assert_eq!(err, DomainError::EmptyTitle);
        assert_eq!(note.updated_at, ts("2026-09-13T12:00:00Z"));
    }

    #[test]
    fn edit_body_and_move_to_bump_updated_at() {
        let mut note = valid_note();
        note.edit_body("new body".into(), ts("2026-09-13T14:00:00Z"));
        assert_eq!(note.body, "new body");
        assert_eq!(note.updated_at, ts("2026-09-13T14:00:00Z"));
        note.move_to(FolderId("f2".into()), ts("2026-09-13T15:00:00Z"));
        assert_eq!(note.folder_id, FolderId("f2".into()));
        assert_eq!(note.updated_at, ts("2026-09-13T15:00:00Z"));
    }

    #[test]
    fn create_note_rejects_oversized_body() {
        let draft = NoteDraft {
            folder_id: FolderId("f1".into()),
            title: "big".into(),
            body: "x".repeat(MAX_BODY_BYTES + 1),
        };
        let err = Note::new(draft, NoteId("n1".into()), ts("2026-09-13T12:00:00Z"))
            .expect_err("oversized body must be rejected");
        assert_eq!(err, DomainError::BodyTooLarge(MAX_BODY_BYTES));
    }

    #[test]
    fn timestamps_only_change_through_explicit_operations() {
        let note = valid_note();
        // No operation ran: created_at and updated_at stay identical.
        assert_eq!(note.created_at, note.updated_at);
        assert!(note.is_live());
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

    fn valid_note() -> Note {
        Note::new(
            NoteDraft {
                folder_id: FolderId("f1".into()),
                title: "title".into(),
                body: "body".into(),
            },
            NoteId("n1".into()),
            ts("2026-09-13T12:00:00Z"),
        )
        .expect("valid note")
    }
}
