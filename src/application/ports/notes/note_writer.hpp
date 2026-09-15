#pragma once
#include "application/result.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_id.hpp"

namespace notes::application {

class NoteWriter {
public:
  virtual ~NoteWriter() = default;
  // CAS: note.revision is the expected base revision before save.
  // On success, store assigns note.revision+1 (or returns saved note).
  [[nodiscard]] virtual Result<domain::Note> save(const domain::Note& note) = 0;
  [[nodiscard]] virtual Result<void> remove(const domain::NoteId& id) = 0;
};

}  // namespace notes::application
