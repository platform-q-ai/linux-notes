#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/notes/note_id.hpp"

namespace notes::application {

// Default delete path: soft-delete into trash (not permanent).
// Prefer TrashNote by name at call sites; this remains for existing wiring.
class DeleteNote {
public:
  DeleteNote(NoteWriter& writer, Clock& clock)
      : writer_(writer), clock_(clock) {}

  // Backward-compatible ctor: clock required for trash timestamp.
  explicit DeleteNote(NoteWriter& writer) = delete;

  [[nodiscard]] Result<void> execute(const domain::NoteId& id) {
    if (id.empty()) {
      return Result<void>::fail({ErrorKind::ValidationFailed, "note id required"});
    }
    return writer_.trash(id, clock_.now_ms());
  }

private:
  NoteWriter& writer_;
  Clock& clock_;
};

}  // namespace notes::application
