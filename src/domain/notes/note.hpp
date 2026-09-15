#pragma once
#include "note_content.hpp"
#include "note_id.hpp"
#include "domain/folders/folder_id.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace notes::domain {

struct Note {
  NoteId id;
  FolderId folder_id;
  std::string title;
  NoteContent content;
  std::int64_t created_at_ms{0};
  std::int64_t modified_at_ms{0};
  std::int64_t revision{0};
  bool pinned{false};
};

struct NoteSummary {
  NoteId id;
  FolderId folder_id;
  std::string title;
  std::string preview;
  std::int64_t modified_at_ms{0};
  std::int64_t revision{0};
  bool pinned{false};
};

}  // namespace notes::domain
