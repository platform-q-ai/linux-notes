#pragma once
#include "application/ports/notes/note_searcher.hpp"
#include "application/result.hpp"
#include "domain/notes/note.hpp"

#include <string>
#include <vector>

namespace notes::application {

class SearchNotes {
public:
  explicit SearchNotes(NoteSearcher& searcher) : searcher_(searcher) {}

  [[nodiscard]] Result<std::vector<domain::NoteSummary>> execute(const std::string& query) const {
    return searcher_.search(query);
  }

private:
  NoteSearcher& searcher_;
};

}  // namespace notes::application
