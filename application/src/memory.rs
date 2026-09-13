//! In-memory implementations of the ports: the shared "fake" used by use-case tests
//! and the contract suite. The SQLite adapter must honor the same contract.

use std::collections::HashMap;
use std::sync::Mutex;

use rusty_notes_domain::{Folder, FolderId, Note, NoteId, Timestamp};

use crate::error::{RepoError, SearchError};
use crate::ports::{Clock, Ids, NoteRepository, SearchService};

struct Store {
    notes: HashMap<NoteId, Note>,
    deleted: HashMap<NoteId, Note>,
    folders: HashMap<FolderId, Folder>,
}

impl Store {
    fn new() -> Self {
        Self {
            notes: HashMap::new(),
            deleted: HashMap::new(),
            folders: HashMap::new(),
        }
    }
}

/// Thread-safe in-memory [`NoteRepository`]. State survives `clone()` (shared
/// `Arc`-backed store), so a "reopen" test can construct a second repository over
/// the same store and observe the same data.
#[derive(Clone)]
pub struct InMemoryNoteRepository {
    store: std::sync::Arc<Mutex<Store>>,
}

impl Default for InMemoryNoteRepository {
    fn default() -> Self {
        Self {
            store: std::sync::Arc::new(Mutex::new(Store::new())),
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
        if store.notes.contains_key(&note.id) || store.deleted.contains_key(&note.id) {
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

/// Case-insensitive substring search over title and body of live notes, ranked by
/// (occurrences desc, updated_at desc, id asc). Honors the same `SearchService`
/// contract as the FTS5 adapter.
#[derive(Clone)]
pub struct InMemorySearch {
    repo: InMemoryNoteRepository,
}

impl InMemorySearch {
    pub fn new(repo: InMemoryNoteRepository) -> Self {
        Self { repo }
    }
}

fn count_occurrences(haystack: &str, needle: &str) -> usize {
    if needle.is_empty() {
        return 0;
    }
    haystack
        .to_lowercase()
        .matches(&needle.to_lowercase())
        .count()
}

impl SearchService for InMemorySearch {
    fn search(&self, query: &str) -> Result<Vec<NoteId>, SearchError> {
        let query = query.trim();
        if query.is_empty() {
            return Err(SearchError::EmptyQuery);
        }
        let notes = self
            .repo
            .list_all_notes()
            .map_err(|e| SearchError::Storage(e.to_string()))?;
        let mut scored: Vec<(usize, &Note)> = notes
            .iter()
            .map(|n| {
                let hits =
                    count_occurrences(&n.title, query) * 2 + count_occurrences(&n.body, query);
                (hits, n)
            })
            .filter(|(hits, _)| *hits > 0)
            .collect();
        scored.sort_by(|a, b| b.0.cmp(&a.0).then_with(|| by_updated_desc(a.1, b.1)));
        Ok(scored.into_iter().map(|(_, n)| n.id.clone()).collect())
    }
}

/// Deterministic [`Clock`] for tests.
pub struct FixedClock {
    now: Mutex<Timestamp>,
}

impl FixedClock {
    pub fn new(now: Timestamp) -> Self {
        Self {
            now: Mutex::new(now),
        }
    }

    /// Advances the clock (used to test `updated_at` ordering).
    pub fn advance(&self, next: Timestamp) {
        *self.now.lock().expect("clock lock") = next;
    }
}

impl Clock for FixedClock {
    fn now(&self) -> Timestamp {
        self.now.lock().expect("clock lock").clone()
    }
}

/// Sequential, human-readable ids (`note-1`, `folder-2`, ...) for tests.
#[derive(Default)]
pub struct SequentialIds {
    counter: Mutex<u64>,
}

impl SequentialIds {
    fn next(&self, prefix: &str) -> String {
        let mut counter = self.counter.lock().expect("counter lock");
        *counter += 1;
        format!("{prefix}-{}", *counter)
    }
}

impl Ids for SequentialIds {
    fn next_note_id(&self) -> NoteId {
        NoteId(self.next("note"))
    }

    fn next_folder_id(&self) -> FolderId {
        FolderId(self.next("folder"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusty_notes_domain::NoteDraft;

    #[test]
    fn in_memory_repo_round_trip() {
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

    #[test]
    fn in_memory_search_ranks_title_hits_above_body_hits() {
        let repo = InMemoryNoteRepository::default();
        repo.insert_folder(
            &Folder::new("Inbox".into(), FolderId("f".into()), Timestamp("t0".into())).unwrap(),
        )
        .unwrap();
        let mk = |id: &str, title: &str, body: &str, at: &str| {
            Note::new(
                NoteDraft {
                    folder_id: FolderId("f".into()),
                    title: title.into(),
                    body: body.into(),
                },
                NoteId(id.into()),
                Timestamp(at.into()),
            )
            .unwrap()
        };
        repo.insert_note(&mk("a", "budget report", "nothing here", "t1"))
            .unwrap();
        repo.insert_note(&mk("b", "random", "budget line", "t2"))
            .unwrap();
        let search = InMemorySearch::new(repo);
        let hits = search.search("budget").expect("search");
        assert_eq!(hits.len(), 2);
        assert_eq!(hits[0], NoteId("a".into()));
        assert_eq!(search.search("  ").unwrap_err(), SearchError::EmptyQuery);
    }
}
