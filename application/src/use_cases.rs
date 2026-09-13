//! Use cases: plain structs over the ports. No framework, no IO, no async.

use std::sync::Arc;

use rusty_notes_domain::{Folder, FolderId, Note, NoteDraft, NoteId};

use crate::error::AppError;
use crate::ports::{Clock, Ids, NoteRepository, SearchService};

/// Creates a note after validating the draft against domain invariants.
pub struct CreateNote {
    repo: Arc<dyn NoteRepository>,
    ids: Arc<dyn Ids>,
    clock: Arc<dyn Clock>,
}

impl CreateNote {
    pub fn new(repo: Arc<dyn NoteRepository>, ids: Arc<dyn Ids>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, ids, clock }
    }

    pub fn execute(&self, draft: NoteDraft) -> Result<Note, AppError> {
        if self.repo.get_folder(&draft.folder_id)?.is_none() {
            return Err(AppError::FolderMissing);
        }
        let id = self.ids.next_note_id();
        let note = Note::new(draft, id, self.clock.now())?;
        self.repo.insert_note(&note)?;
        Ok(note)
    }
}

/// Updates title and/or body of an existing note through domain operations.
pub struct UpdateNote {
    repo: Arc<dyn NoteRepository>,
    clock: Arc<dyn Clock>,
}

impl UpdateNote {
    pub fn new(repo: Arc<dyn NoteRepository>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, clock }
    }

    pub fn execute(
        &self,
        id: &NoteId,
        title: Option<String>,
        body: Option<String>,
    ) -> Result<Note, AppError> {
        let mut note = self.repo.get_note(id)?.ok_or(AppError::NoteMissing)?;
        if let Some(title) = title {
            note.rename(title, self.clock.now())?;
        }
        if let Some(body) = body {
            note.edit_body(body, self.clock.now());
        }
        self.repo.update_note(&note)?;
        Ok(note)
    }
}

/// Moves a note into another folder (Apple-Notes-style drag between folders).
pub struct MoveNote {
    repo: Arc<dyn NoteRepository>,
    clock: Arc<dyn Clock>,
}

impl MoveNote {
    pub fn new(repo: Arc<dyn NoteRepository>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, clock }
    }

    pub fn execute(&self, id: &NoteId, target: FolderId) -> Result<Note, AppError> {
        if self.repo.get_folder(&target)?.is_none() {
            return Err(AppError::FolderMissing);
        }
        let mut note = self.repo.get_note(id)?.ok_or(AppError::NoteMissing)?;
        note.move_to(target, self.clock.now());
        self.repo.update_note(&note)?;
        Ok(note)
    }
}

/// Soft-deletes a note.
pub struct DeleteNote {
    repo: Arc<dyn NoteRepository>,
}

impl DeleteNote {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self, id: &NoteId) -> Result<(), AppError> {
        self.repo.soft_delete_note(id)?;
        Ok(())
    }
}

/// Lists notes, optionally scoped to one folder.
pub struct ListNotes {
    repo: Arc<dyn NoteRepository>,
}

impl ListNotes {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self, folder: Option<FolderId>) -> Result<Vec<Note>, AppError> {
        let notes = match folder {
            Some(id) => self.repo.list_notes_by_folder(&id)?,
            None => self.repo.list_all_notes()?,
        };
        Ok(notes)
    }
}

/// Lists folders in name order.
pub struct ListFolders {
    repo: Arc<dyn NoteRepository>,
}

impl ListFolders {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self) -> Result<Vec<Folder>, AppError> {
        Ok(self.repo.list_folders()?)
    }
}

/// Creates a folder.
pub struct CreateFolder {
    repo: Arc<dyn NoteRepository>,
    ids: Arc<dyn Ids>,
    clock: Arc<dyn Clock>,
}

impl CreateFolder {
    pub fn new(repo: Arc<dyn NoteRepository>, ids: Arc<dyn Ids>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, ids, clock }
    }

    pub fn execute(&self, name: String) -> Result<Folder, AppError> {
        let id = self.ids.next_folder_id();
        let folder = Folder::new(name, id, self.clock.now())?;
        self.repo.insert_folder(&folder)?;
        Ok(folder)
    }
}

/// Renames a folder through the domain invariant.
pub struct RenameFolder {
    repo: Arc<dyn NoteRepository>,
    clock: Arc<dyn Clock>,
}

impl RenameFolder {
    pub fn new(repo: Arc<dyn NoteRepository>, clock: Arc<dyn Clock>) -> Self {
        Self { repo, clock }
    }

    pub fn execute(&self, id: &FolderId, name: String) -> Result<Folder, AppError> {
        let mut folder = self.repo.get_folder(id)?.ok_or(AppError::FolderMissing)?;
        folder.rename(name, self.clock.now())?;
        self.repo.update_folder(&folder)?;
        Ok(folder)
    }
}

/// Deletes an empty folder only.
pub struct DeleteFolder {
    repo: Arc<dyn NoteRepository>,
}

impl DeleteFolder {
    pub fn new(repo: Arc<dyn NoteRepository>) -> Self {
        Self { repo }
    }

    pub fn execute(&self, id: &FolderId) -> Result<(), AppError> {
        self.repo.delete_folder(id)?;
        Ok(())
    }
}

/// Searches notes and resolves hits to full notes, best match first.
pub struct SearchNotes {
    search: Arc<dyn SearchService>,
    repo: Arc<dyn NoteRepository>,
}

impl SearchNotes {
    pub fn new(search: Arc<dyn SearchService>, repo: Arc<dyn NoteRepository>) -> Self {
        Self { search, repo }
    }

    pub fn execute(&self, query: &str) -> Result<Vec<Note>, AppError> {
        let ids = self.search.search(query)?;
        let mut notes = Vec::with_capacity(ids.len());
        for id in ids {
            // The index only contains live notes; a missing row is a lagging index,
            // not a user-facing error.
            if let Some(note) = self.repo.get_note(&id)? {
                notes.push(note);
            }
        }
        Ok(notes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::memory::{FixedClock, InMemoryNoteRepository, InMemorySearch, SequentialIds};
    use rusty_notes_domain::Timestamp;

    pub(super) struct Wired {
        pub(super) repo: Arc<InMemoryNoteRepository>,
        pub(super) create_note: CreateNote,
        pub(super) update_note: UpdateNote,
        pub(super) move_note: MoveNote,
        pub(super) delete_note: DeleteNote,
        pub(super) list_notes: ListNotes,
        pub(super) list_folders: ListFolders,
        pub(super) create_folder: CreateFolder,
        pub(super) rename_folder: RenameFolder,
        pub(super) delete_folder: DeleteFolder,
        pub(super) search_notes: SearchNotes,
        pub(super) clock: Arc<FixedClock>,
    }

    pub(super) fn wired() -> Wired {
        let repo = Arc::new(InMemoryNoteRepository::default());
        let search = Arc::new(InMemorySearch::new(InMemoryNoteRepository::clone(&repo)));
        let ids = Arc::new(SequentialIds::default());
        let clock = Arc::new(FixedClock::new(Timestamp("2026-09-13T12:00:00Z".into())));
        Wired {
            create_note: CreateNote::new(repo.clone(), ids.clone(), clock.clone()),
            update_note: UpdateNote::new(repo.clone(), clock.clone()),
            move_note: MoveNote::new(repo.clone(), clock.clone()),
            delete_note: DeleteNote::new(repo.clone()),
            list_notes: ListNotes::new(repo.clone()),
            list_folders: ListFolders::new(repo.clone()),
            create_folder: CreateFolder::new(repo.clone(), ids.clone(), clock.clone()),
            rename_folder: RenameFolder::new(repo.clone(), clock.clone()),
            delete_folder: DeleteFolder::new(repo.clone()),
            search_notes: SearchNotes::new(search, repo.clone()),
            clock,
            repo,
        }
    }

    #[test]
    fn move_note_flow_updates_folder_and_timestamp() {
        let w = wired();
        let folder = w.create_folder.execute("Work".into()).expect("folder");
        let other = w.create_folder.execute("Archive".into()).expect("folder");
        let note = w
            .create_note
            .execute(NoteDraft {
                folder_id: folder.id.clone(),
                title: "agenda".into(),
                body: String::new(),
            })
            .expect("note");

        let moved = w
            .move_note
            .execute(&note.id, other.id.clone())
            .expect("move ok");
        assert_eq!(moved.folder_id, other.id);
        assert_eq!(moved.updated_at, Timestamp("2026-09-13T12:00:00Z".into()));

        w.clock.advance(Timestamp("2026-09-13T13:00:00Z".into()));
        let _ = w
            .update_note
            .execute(&note.id, None, Some("touched".into()))
            .expect("touch");

        let in_old = w
            .list_notes
            .execute(Some(folder.id.clone()))
            .expect("list old folder");
        assert!(in_old.is_empty());
        let in_new = w
            .list_notes
            .execute(Some(other.id))
            .expect("list new folder");
        assert_eq!(in_new.len(), 1);
        assert_eq!(
            in_new[0].updated_at,
            Timestamp("2026-09-13T13:00:00Z".into())
        );
    }

    #[test]
    fn move_note_to_missing_folder_is_folder_missing() {
        let w = wired();
        let folder = w.create_folder.execute("Work".into()).expect("folder");
        let note = w
            .create_note
            .execute(NoteDraft {
                folder_id: folder.id.clone(),
                title: "t".into(),
                body: String::new(),
            })
            .expect("note");
        let err = w
            .move_note
            .execute(&note.id, FolderId("nope".into()))
            .expect_err("missing folder");
        assert!(matches!(err, AppError::FolderMissing));
    }

    #[test]
    fn search_then_open_flow_returns_resolved_notes() {
        let w = wired();
        let folder = w.create_folder.execute("Inbox".into()).expect("folder");
        w.create_note
            .execute(NoteDraft {
                folder_id: folder.id.clone(),
                title: "Quarterly budget".into(),
                body: "spreadsheet numbers".into(),
            })
            .expect("note 1");
        w.create_note
            .execute(NoteDraft {
                folder_id: folder.id.clone(),
                title: "Groceries".into(),
                body: "milk".into(),
            })
            .expect("note 2");

        let hits = w.search_notes.execute("BUDGET").expect("search ok");
        assert_eq!(hits.len(), 1);
        assert_eq!(hits[0].title, "Quarterly budget");

        let err = w.search_notes.execute("   ").expect_err("blank query");
        assert!(matches!(err, AppError::Search(_)));
    }

    #[test]
    fn delete_folder_with_notes_is_rejected() {
        let w = wired();
        let folder = w.create_folder.execute("Inbox".into()).expect("folder");
        let note = w
            .create_note
            .execute(NoteDraft {
                folder_id: folder.id.clone(),
                title: "t".into(),
                body: String::new(),
            })
            .expect("note");
        let err = w
            .delete_folder
            .execute(&folder.id)
            .expect_err("folder has notes");
        assert!(
            matches!(err, AppError::Repo(crate::error::RepoError::FolderHasNotes)),
            "unexpected error: {err:?}"
        );

        // Soft-delete the note; per contract the folder still is not deletable:
        // soft-deleted notes keep referencing it until purged.
        w.delete_note.execute(&note.id).expect("soft delete ok");
        w.delete_folder
            .execute(&folder.id)
            .expect_err("soft-deleted note still references the folder");
        let _ = w.repo.live_note_count();
    }

    #[test]
    fn list_folders_returns_name_order() {
        let w = wired();
        let _work = w.create_folder.execute("Work".into()).expect("work");
        let _archive = w.create_folder.execute("Archive".into()).expect("archive");
        let folders = w.list_folders.execute().expect("list folders");
        let names: Vec<&str> = folders.iter().map(|f| f.name.as_str()).collect();
        assert_eq!(names, vec!["Archive", "Work"]);
    }
}

#[cfg(test)]
mod rename_tests {
    use super::tests::wired;
    use super::*;

    #[test]
    fn rename_folder_updates_name_and_bumps_updated_at() {
        let w = wired();
        let folder = w.create_folder.execute("Inbox".into()).expect("folder");
        assert_eq!(folder.created_at, folder.updated_at);

        w.clock
            .advance(rusty_notes_domain::Timestamp("2026-09-13T13:00:00Z".into()));
        let renamed = w
            .rename_folder
            .execute(&folder.id, "Work".into())
            .expect("rename ok");
        assert_eq!(renamed.name, "Work");
        assert_eq!(
            renamed.updated_at,
            rusty_notes_domain::Timestamp("2026-09-13T13:00:00Z".into())
        );

        let listed = w.list_folders.execute().expect("list folders");
        assert_eq!(listed.len(), 1);
        assert_eq!(listed[0].name, "Work");

        let err = w
            .rename_folder
            .execute(&FolderId("ghost".into()), "x".into())
            .expect_err("missing folder");
        assert!(matches!(err, AppError::FolderMissing));
    }
}
