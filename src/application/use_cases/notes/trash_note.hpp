#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/notes/note_id.hpp"

namespace notes::application {

// Soft-delete: move note into trash (default delete path).
class TrashNote {
public:
  TrashNote(NoteWriter& writer, Clock& clock) : writer_(writer), clock_(clock) {}

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
