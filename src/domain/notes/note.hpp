#pragma once
#include "note_content.hpp"
#include "note_id.hpp"
#include "domain/folders/folder_id.hpp"

#include <cstdint>
#include <optional>
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
  // Soft-delete: trashed_at_ms > 0 means the note is in trash.
  std::int64_t trashed_at_ms{0};
  // Folder the note lived in when trashed (restore target). Empty if never trashed.
  std::optional<FolderId> trashed_from_folder_id;

  [[nodiscard]] bool is_trashed() const noexcept { return trashed_at_ms > 0; }
};

struct NoteSummary {
  NoteId id;
  FolderId folder_id;
  std::string title;
  std::string preview;
  std::int64_t modified_at_ms{0};
  std::int64_t revision{0};
  bool pinned{false};
  std::int64_t trashed_at_ms{0};

  [[nodiscard]] bool is_trashed() const noexcept { return trashed_at_ms > 0; }
};

}  // namespace notes::domain
