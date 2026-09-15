#pragma once

#include "application/ports/attachments/attachment_store.hpp"
#include "application/ports/clock.hpp"

#include <filesystem>

namespace notes::adapters::attachments {

class FilesystemAttachmentStore final : public application::AttachmentStore {
public:
  FilesystemAttachmentStore(std::filesystem::path root,
                            application::Clock& clock);

  [[nodiscard]] application::Result<domain::Attachment> put(
      const domain::NoteId& note_id, const std::string& file_name,
      const std::string& mime_type,
      const std::vector<std::uint8_t>& bytes) override;

  [[nodiscard]] application::Result<std::vector<std::uint8_t>> get(
      const domain::AttachmentId& id) const override;

  [[nodiscard]] application::Result<void> remove(
      const domain::AttachmentId& id) override;

private:
  [[nodiscard]] std::filesystem::path path_for(
      const domain::AttachmentId& id) const;

  std::filesystem::path root_;
  application::Clock& clock_;
  std::uint64_t seq_{0};
};

}  // namespace notes::adapters::attachments
