#pragma once
#include "application/ports/notes/note_reader.hpp"
#include "application/result.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_id.hpp"

namespace notes::application {

class LoadNote {
public:
  explicit LoadNote(NoteReader& reader) : reader_(reader) {}

  [[nodiscard]] Result<domain::Note> execute(const domain::NoteId& id) const {
    if (id.empty()) {
      return Result<domain::Note>::fail({ErrorKind::ValidationFailed, "note id required"});
    }
    return reader_.load(id);
  }

  // Exposed for attachment unref/GC that must scan other notes.
  [[nodiscard]] NoteReader& reader() noexcept { return reader_; }
  [[nodiscard]] const NoteReader& reader() const noexcept { return reader_; }

private:
  NoteReader& reader_;
};

}  // namespace notes::application
