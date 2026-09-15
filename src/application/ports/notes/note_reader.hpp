#pragma once
#include "application/result.hpp"
#include "domain/folders/folder_id.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_id.hpp"

#include <vector>

namespace notes::application {

class NoteReader {
public:
  virtual ~NoteReader() = default;
  [[nodiscard]] virtual Result<domain::Note> load(const domain::NoteId& id) const = 0;
  [[nodiscard]] virtual Result<std::vector<domain::NoteSummary>> list(
      const domain::FolderId& folder_id) const = 0;
};

}  // namespace notes::application
