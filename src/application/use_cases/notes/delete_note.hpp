#pragma once
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/notes/note_id.hpp"

namespace notes::application {

class DeleteNote {
public:
  explicit DeleteNote(NoteWriter& writer) : writer_(writer) {}

  [[nodiscard]] Result<void> execute(const domain::NoteId& id) {
    if (id.empty()) {
      return Result<void>::fail({ErrorKind::ValidationFailed, "note id required"});
    }
    return writer_.remove(id);
  }

private:
  NoteWriter& writer_;
};

}  // namespace notes::application
