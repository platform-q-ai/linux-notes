#pragma once

#include "application/ports/attachments/attachment_store.hpp"
#include "application/ports/clock.hpp"

#include <map>
#include <utility>

namespace notes::testing {

class InMemoryAttachmentStore final : public application::AttachmentStore {
public:
  explicit InMemoryAttachmentStore(application::Clock& clock) : clock_(clock) {}

  [[nodiscard]] application::Result<domain::Attachment> put(
      const domain::NoteId& note_id, const std::string& file_name,
      const std::string& mime_type,
      const std::vector<std::uint8_t>& bytes) override {
    domain::Attachment att;
    att.id = domain::AttachmentId{"att-mem-" + std::to_string(++seq_)};
    att.note_id = note_id;
    att.file_name = file_name;
    att.mime_type =
        mime_type.empty() ? "application/octet-stream" : mime_type;
    att.byte_size = static_cast<std::int64_t>(bytes.size());
    att.created_at_ms = clock_.now_ms();
    bytes_[att.id] = bytes;
    meta_[att.id] = att;
    return application::Result<domain::Attachment>::ok(att);
  }

  [[nodiscard]] application::Result<std::vector<std::uint8_t>> get(
      const domain::AttachmentId& id) const override {
    auto it = bytes_.find(id);
    if (it == bytes_.end()) {
      return application::Result<std::vector<std::uint8_t>>::fail(
          {application::ErrorKind::NotFound, "attachment not found"});
    }
    return application::Result<std::vector<std::uint8_t>>::ok(it->second);
  }

  [[nodiscard]] application::Result<void> remove(
      const domain::AttachmentId& id) override {
    bytes_.erase(id);
    meta_.erase(id);
    return application::Result<void>::ok();
  }

private:
  application::Clock& clock_;
  std::uint64_t seq_{0};
  std::map<domain::AttachmentId, std::vector<std::uint8_t>> bytes_;
  std::map<domain::AttachmentId, domain::Attachment> meta_;
};

}  // namespace notes::testing
