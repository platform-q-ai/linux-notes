//! [`SequentialIds`]: sequential test ids. Test-only: see crate `lib.rs`.

use std::sync::Mutex;

use rusty_notes_application::ports::Ids;
use rusty_notes_domain::{FolderId, NoteId};

/// Sequential, human-readable ids (`note-1`, `folder-2`, ...) for tests.
///
/// TEST-ONLY on purpose (thread 2 / F8): the counter lives in process memory, so
/// ids repeat after a restart over an existing database — that restart bug is
/// exactly why the production composition root wires `rusty_notes_system::UniqueIds`
/// instead of this fake.
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

    #[test]
    fn sequential_ids_count_up_and_are_documented_test_only() {
        let ids = SequentialIds::default();
        assert_eq!(ids.next_note_id().0, "note-1");
        assert_eq!(ids.next_folder_id().0, "folder-2");
        assert_eq!(ids.next_note_id().0, "note-3");
    }
}
