//! The `Note` entity: business invariants of the primary aggregate.
//!
//! Soft-delete state is a persistence concern behind the `NoteRepository` port in
//! the application layer, not a domain flag — the former constant-true `is_live`
//! predicate was dead API and was removed rather than carried into this module.

/// Hard cap on note body size (1 MiB). Small enough to keep UI list views and
/// any later sync payload sane, large enough that no real note hits it.
///
/// Enforced on every body mutation path (`Note::new`, `Note::edit_body`), not
/// only at creation — the invariant is a property of a valid `Note`, full stop.
pub const MAX_BODY_BYTES: usize = 1024 * 1024;

use crate::error::DomainError;
use crate::FolderId;
use crate::NoteDraft;
use crate::NoteId;
use crate::Timestamp;

/// A note. `updated_at` only changes through explicit operations (rename, edit_body,
/// move_to) — never implicitly, and never on a rejected operation.
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
    /// Creates a note, enforcing the non-empty title and capped-body invariants.
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

    /// Replaces the body. Bodies may be empty, but never over `MAX_BODY_BYTES`:
    /// the cap is enforced on this mutation path exactly as in `Note::new`
    /// (behavior thread 4). On rejection, `updated_at` is untouched.
    pub fn edit_body(&mut self, body: String, now: Timestamp) -> Result<(), DomainError> {
        if body.len() > MAX_BODY_BYTES {
            return Err(DomainError::BodyTooLarge(MAX_BODY_BYTES));
        }
        self.body = body;
        self.updated_at = now;
        Ok(())
    }

    /// Moves the note into another folder.
    pub fn move_to(&mut self, folder: FolderId, now: Timestamp) {
        self.folder_id = folder;
        self.updated_at = now;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ts(value: &str) -> Timestamp {
        Timestamp(value.to_string())
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
        note.edit_body("new body".into(), ts("2026-09-13T14:00:00Z"))
            .expect("valid body edit");
        assert_eq!(note.body, "new body");
        assert_eq!(note.updated_at, ts("2026-09-13T14:00:00Z"));
        note.move_to(FolderId("f2".into()), ts("2026-09-13T15:00:00Z"));
        assert_eq!(note.folder_id, FolderId("f2".into()));
        assert_eq!(note.updated_at, ts("2026-09-13T15:00:00Z"));
    }

    // Behavior thread 4 regression: the body cap must hold on every mutation
    // path, not only in `Note::new`.

    #[test]
    fn edit_body_rejects_oversized_body() {
        let mut note = valid_note();
        let err = note
            .edit_body("x".repeat(MAX_BODY_BYTES + 1), ts("2026-09-13T14:00:00Z"))
            .expect_err("oversized body must be rejected on edit too");
        assert_eq!(err, DomainError::BodyTooLarge(MAX_BODY_BYTES));
    }

    #[test]
    fn edit_body_rejection_leaves_body_and_updated_at_untouched() {
        let mut note = valid_note();
        let err = note
            .edit_body("x".repeat(MAX_BODY_BYTES + 1), ts("2026-09-13T14:00:00Z"))
            .expect_err("oversized body must be rejected");
        assert_eq!(err, DomainError::BodyTooLarge(MAX_BODY_BYTES));
        assert_eq!(note.body, "body", "rejected edit must not change the body");
        assert_eq!(
            note.updated_at,
            ts("2026-09-13T12:00:00Z"),
            "rejected edit must not bump updated_at"
        );
    }

    #[test]
    fn edit_body_accepts_body_at_exactly_the_cap_and_empty_body() {
        let mut note = valid_note();
        note.edit_body("x".repeat(MAX_BODY_BYTES), ts("2026-09-13T14:00:00Z"))
            .expect("body of exactly MAX_BODY_BYTES must be accepted");
        assert_eq!(note.body.len(), MAX_BODY_BYTES);
        note.edit_body(String::new(), ts("2026-09-13T15:00:00Z"))
            .expect("bodies may be empty");
        assert_eq!(note.body, "");
    }

    #[test]
    fn timestamps_only_change_through_explicit_operations() {
        let note = valid_note();
        // No operation ran: created_at and updated_at stay identical.
        assert_eq!(note.created_at, note.updated_at);
    }
}
