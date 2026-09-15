#pragma once
#include "application/result.hpp"
#include "domain/notes/note.hpp"

#include <string>
#include <vector>

namespace notes::application {

class NoteSearcher {
public:
  virtual ~NoteSearcher() = default;
  [[nodiscard]] virtual Result<std::vector<domain::NoteSummary>> search(
      const std::string& query) const = 0;
};

}  // namespace notes::application
