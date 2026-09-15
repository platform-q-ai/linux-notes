#pragma once
#include "application/result.hpp"
#include "domain/attachments/attachment.hpp"
#include "domain/attachments/attachment_id.hpp"
#include "domain/notes/note_id.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace notes::application {

class AttachmentStore {
public:
  virtual ~AttachmentStore() = default;
  [[nodiscard]] virtual Result<domain::Attachment> put(
      const domain::NoteId& note_id,
      const std::string& file_name,
      const std::string& mime_type,
      const std::vector<std::uint8_t>& bytes) = 0;
  [[nodiscard]] virtual Result<std::vector<std::uint8_t>> get(
      const domain::AttachmentId& id) const = 0;
  [[nodiscard]] virtual Result<void> remove(const domain::AttachmentId& id) = 0;
};

}  // namespace notes::application
