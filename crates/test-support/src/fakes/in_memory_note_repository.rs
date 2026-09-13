//! Thread-safe in-memory [`NoteRepository`] fake: the shared fake used by
//! use-case tests and the contract suite. Test-only: see crate `lib.rs`.
//!
//! The SQLite adapter must honor the same behavioral contract (see
//! `contracts::repository_contract`).

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use rusty_notes_application::error::RepoError;
use rusty_notes_application::ports::NoteRepository;
use rusty_notes_domain::{Folder, FolderId, Note, NoteId};

struct Store {
    notes: HashMap<NoteId, Note>,
    deleted: HashMap<NoteId, Note>,
    purged: HashMap<NoteId, Note>,
    folders: HashMap<FolderId, Folder>,
}

impl Store {
    fn new() -> Self {
        Self {
            notes: HashMap::new(),
            deleted: HashMap::new(),
            purged: HashMap::new(),
            folders: HashMap::new(),
        }
    }
}

/// Thread-safe in-memory [`NoteRepository`]. State survives `clone()` (shared
/// `Arc`-backed store), so a "reopen" test can construct a second repository over
/// the same store and observe the same data.
#[derive(Clone)]
pub struct InMemoryNoteRepository {
    store: Arc<Mutex<Store>>,
}

impl Default for InMemoryNoteRepository {
    fn default() -> Self {
        Self {
            store: Arc::new(Mutex::new(Store::new())),
        }
    }
}

impl InMemoryNoteRepository {
    /// Number of live notes (test introspection helper).
    pub fn live_note_count(&self) -> usize {
        self.store.lock().expect("store lock").notes.len()
    }

    /// Returns a soft-deleted note, if any (test introspection helper).
    pub fn deleted_note(&self, id: &NoteId) -> Option<Note> {
        self.store
            .lock()
            .expect("store lock")
            .deleted
            .get(id)
            .cloned()
    }

    /// Returns a purged note, if any (test introspection helper; soft-delete
    /// lifecycle contract, thread 6).
    pub fn purged_note(&self, id: &NoteId) -> Option<Note> {
        self.store
            .lock()
            .expect("store lock")
            .purged
            .get(id)
            .cloned()
    }
}

fn by_updated_desc(a: &Note, b: &Note) -> std::cmp::Ordering {
    // Newest first; equal `updated_at` keeps "later-created first" by falling
    // back to id DESC (ids increase over time), so ties stay deterministic
    // without exposing per-row insertion counters through the port.
    b.updated_at
        .cmp(&a.updated_at)
        .then_with(|| b.id.0.cmp(&a.id.0))
}

impl NoteRepository for InMemoryNoteRepository {
    fn insert_note(&self, note: &Note) -> Result<(), RepoError> {
        let mut store = self.store.lock().expect("store lock");
        if store.notes.contains_key(&note.id)
            || store.deleted.contains_key(&note.id)
            || store.purged.contains_key(&note.id)
        {
            return Err(RepoError::Conflict);
        }
        if !store.folders.contains_key(&note.folder_id) {
            return Err(RepoError::NotFound);
        }
        store.notes.insert(note.id.clone(), note.clone());
        Ok(())
    }

    fn get_note(&self, id: &NoteId) -> Result<Option<Note>, RepoError> {
        let store = self.store.lock().expect("store lock");
        Ok(store.notes.get(id).cloned())
    }

    fn get_note_including_deleted(&self, id: &NoteId) -> Result<Option<Note>, RepoError> {
        let store = self.store.lock().expect("store lock");
        Ok(store
            .notes
            .get(id)
            .or_else(|| store.deleted.get(id))
            .cloned())
    }

    fn purge_note(&self, id: &NoteId) -> Result<(), RepoError> {
        let mut store = self.store.lock().expect("store lock");
        match store.deleted.remove(id) {
            Some(note) => {
                store.purged.insert(id.clone(), note);
                Ok(())
            }
            None => Err(RepoError::NotFound),
        }
    }

    fn update_note(&self, note: &Note) -> Result<(), RepoError> {
        let mut store = self.store.lock().expect("store lock");
        if !store.folders.contains_key(&note.folder_id) {
            return Err(RepoError::NotFound);
        }
        match store.notes.get_mut(&note.id) {
            Some(existing) => {
                *existing = note.clone();
                Ok(())
            }
            None => Err(RepoError::NotFound),
        }
    }

    fn soft_delete_note(&self, id: &NoteId) -> Result<(), RepoError> {
        let mut store = self.store.lock().expect("store lock");
        match store.notes.remove(id) {
            Some(note) => {
                store.deleted.insert(id.clone(), note);
                Ok(())
            }
            None => Err(RepoError::NotFound),
        }
    }

    fn list_notes_by_folder(&self, folder_id: &FolderId) -> Result<Vec<Note>, RepoError> {
        let store = self.store.lock().expect("store lock");
        let mut notes: Vec<Note> = store
            .notes
            .values()
            .filter(|n| &n.folder_id == folder_id)
            .cloned()
            .collect();
        notes.sort_by(by_updated_desc);
        Ok(notes)
    }

    fn list_all_notes(&self) -> Result<Vec<Note>, RepoError> {
        let store = self.store.lock().expect("store lock");
        let mut notes: Vec<Note> = store.notes.values().cloned().collect();
        notes.sort_by(by_updated_desc);
        Ok(notes)
    }

    fn insert_folder(&self, folder: &Folder) -> Result<(), RepoError> {
        let mut store = self.store.lock().expect("store lock");
        if store.folders.contains_key(&folder.id) {
            return Err(RepoError::Conflict);
        }
        store.folders.insert(folder.id.clone(), folder.clone());
        Ok(())
    }

    fn get_folder(&self, id: &FolderId) -> Result<Option<Folder>, RepoError> {
        let store = self.store.lock().expect("store lock");
        Ok(store.folders.get(id).cloned())
    }

    fn list_folders(&self) -> Result<Vec<Folder>, RepoError> {
        let store = self.store.lock().expect("store lock");
        let mut folders: Vec<Folder> = store.folders.values().cloned().collect();
        folders.sort_by(|a, b| a.name.cmp(&b.name).then_with(|| a.id.0.cmp(&b.id.0)));
        Ok(folders)
    }

    fn update_folder(&self, folder: &Folder) -> Result<(), RepoError> {
        let mut store = self.store.lock().expect("store lock");
        match store.folders.get_mut(&folder.id) {
            Some(existing) => {
                *existing = folder.clone();
                Ok(())
            }
            None => Err(RepoError::NotFound),
        }
    }

    fn delete_folder(&self, id: &FolderId) -> Result<(), RepoError> {
        let mut store = self.store.lock().expect("store lock");
        if !store.folders.contains_key(id) {
            return Err(RepoError::NotFound);
        }
        let referenced = store
            .notes
            .values()
            .chain(store.deleted.values())
            .any(|n| &n.folder_id == id);
        if referenced {
            return Err(RepoError::FolderHasNotes);
        }
        store.folders.remove(id);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusty_notes_domain::{NoteDraft, Timestamp};

    #[test]
    fn in_memory_repo_round_trip_and_soft_delete() {
        let repo = InMemoryNoteRepository::default();
        let folder = Folder::new(
            "Inbox".into(),
            FolderId("f1".into()),
            Timestamp("t0".into()),
        )
        .expect("folder");
        repo.insert_folder(&folder).expect("insert folder");
        let note = Note::new(
            NoteDraft {
                folder_id: FolderId("f1".into()),
                title: "t".into(),
                body: "b".into(),
            },
            NoteId("n1".into()),
            Timestamp("t1".into()),
        )
        .expect("note");
        repo.insert_note(&note).expect("insert note");
        assert_eq!(
            repo.get_note(&NoteId("n1".into())).expect("get").unwrap(),
            note
        );
        repo.soft_delete_note(&NoteId("n1".into())).expect("delete");
        assert!(repo.get_note(&NoteId("n1".into())).expect("get").is_none());
        assert!(repo.deleted_note(&NoteId("n1".into())).is_some());
    }
}
