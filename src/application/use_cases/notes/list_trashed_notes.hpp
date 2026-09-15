#pragma once
#include "application/ports/notes/note_reader.hpp"
#include "application/result.hpp"
#include "domain/notes/note.hpp"

#include <vector>

namespace notes::application {

class ListTrashedNotes {
public:
  explicit ListTrashedNotes(NoteReader& reader) : reader_(reader) {}

  [[nodiscard]] Result<std::vector<domain::NoteSummary>> execute() const {
    return reader_.list_trashed();
  }

private:
  NoteReader& reader_;
};

}  // namespace notes::application
