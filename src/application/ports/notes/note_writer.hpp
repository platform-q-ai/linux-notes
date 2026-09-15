#pragma once
#include "application/result.hpp"
#include "domain/folders/folder_id.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_id.hpp"

#include <cstdint>

namespace notes::application {

class NoteWriter {
public:
  virtual ~NoteWriter() = default;
  // CAS: note.revision is the expected base revision before save.
  // On success, store assigns note.revision+1 (or returns saved note).
  // Cannot clear trash via save — restore() is the only path. Trashed rows keep
  // parked trash metadata from storage; stale active snapshots fail CAS after trash.
  [[nodiscard]] virtual Result<domain::Note> save(const domain::Note& note) = 0;

  // Soft-delete: mark trashed_at; keep body/attachments; bump revision.
  // NotFound if missing. Idempotent if already trashed.
  [[nodiscard]] virtual Result<void> trash(const domain::NoteId& id,
                                           std::int64_t trashed_at_ms) = 0;

  // Clear trash flags; place in restore_folder_id (caller resolves missing folder);
  // bump revision.
  [[nodiscard]] virtual Result<domain::Note> restore(
      const domain::NoteId& id, const domain::FolderId& restore_folder_id) = 0;

  // Permanent delete of note row (+ search index). Attachments GC is caller's job.
  [[nodiscard]] virtual Result<void> remove(const domain::NoteId& id) = 0;
};

}  // namespace notes::application
