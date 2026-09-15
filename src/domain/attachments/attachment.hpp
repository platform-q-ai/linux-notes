#pragma once
#include "attachment_id.hpp"
#include "domain/notes/note_id.hpp"

#include <cstdint>
#include <string>

namespace notes::domain {

struct Attachment {
  AttachmentId id;
  NoteId note_id;
  std::string file_name;
  std::string mime_type;
  std::int64_t byte_size{0};
  std::int64_t created_at_ms{0};
};

}  // namespace notes::domain
