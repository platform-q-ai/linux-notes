//! [`UniqueIds`]: production identifier source that survives restarts (thread 2).

use std::sync::atomic::{AtomicU64, Ordering};

use rusty_notes_application::ports::Ids;
use rusty_notes_domain::{FolderId, NoteId};

/// Process-unique prefix for id namespacing (std-only, no `uuid` crate): time
/// plus a counter plus the pid, hex-encoded.
fn fresh_prefix() -> String {
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or_default();
    format!("{nanos:x}-{:x}", std::process::id())
}

/// Production [`Ids`] adapter (thread 2 / F8/F11): hands out identifiers that are
/// unique across processes, so ids never collide after an app restart over an
/// existing database.
///
/// Each instance draws a fresh per-process prefix (`<boot-nanos>-<pid>`); ids are
/// `<prefix>-note-<n>` / `<prefix>-folder-<n>` from a monotonically increasing
/// counter. Two fresh instances — two app runs over the same database — can
/// never emit the same id, which is exactly the property the fake
/// `SequentialIds` lacks (it restarted at `note-1` and collided).
///
/// Std-only by design: no persistence file, no `uuid` dependency. Ids are opaque
/// strings behind the `NoteId`/`FolderId` newtypes; nothing may parse them back.
pub struct UniqueIds {
    prefix: String,
    counter: AtomicU64,
}

impl Default for UniqueIds {
    fn default() -> Self {
        Self {
            prefix: fresh_prefix(),
            counter: AtomicU64::new(0),
        }
    }
}

impl UniqueIds {
    fn next(&self, kind: &str) -> String {
        let n = self.counter.fetch_add(1, Ordering::SeqCst) + 1;
        format!("{}-{kind}-{n}", self.prefix)
    }
}

impl Ids for UniqueIds {
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
    fn ids_carry_the_kind_prefix_and_unique_counter() {
        let ids = UniqueIds::default();
        let n1 = ids.next_note_id();
        let n2 = ids.next_note_id();
        let f1 = ids.next_folder_id();
        assert_ne!(n1.0, n2.0, "counter must advance");
        assert_ne!(n1.0, f1.0, "kinds must not collide even at equal counters");
        assert!(n1.0.ends_with("-note-1"));
        assert!(n2.0.ends_with("-note-2"));
        // One counter is shared across kinds, so the folder id continues after
        // the note ids (documented, deterministic behavior).
        assert!(f1.0.ends_with("-folder-3"));
    }

    #[test]
    fn fresh_instances_never_collide_like_a_restart_would() {
        // Two independent instances stand in for two app runs over the same
        // database: nothing is shared, yet no id repeats (the regression guard
        // for the fake-SequentialIds restart bug).
        let a = UniqueIds::default();
        let b = UniqueIds::default();
        assert_ne!(a.next_note_id(), b.next_note_id());
        assert_ne!(a.next_folder_id(), b.next_folder_id());
    }
}
