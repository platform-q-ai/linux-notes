#pragma once
#include "application/ports/notes/note_reader.hpp"
#include "application/result.hpp"
#include "domain/folders/folder_id.hpp"
#include "domain/notes/note.hpp"

#include <vector>

namespace notes::application {

class ListNotes {
public:
  explicit ListNotes(NoteReader& reader) : reader_(reader) {}

  [[nodiscard]] Result<std::vector<domain::NoteSummary>> execute(
      const domain::FolderId& folder_id) const {
    return reader_.list(folder_id);
  }

private:
  NoteReader& reader_;
};

}  // namespace notes::application
