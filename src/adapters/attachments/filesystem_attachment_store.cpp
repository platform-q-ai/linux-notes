#include "adapters/attachments/filesystem_attachment_store.hpp"

#include <fstream>
#include <system_error>

namespace notes::adapters::attachments {

FilesystemAttachmentStore::FilesystemAttachmentStore(
    std::filesystem::path root, application::Clock& clock)
    : root_(std::move(root)), clock_(clock) {
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
}

std::filesystem::path FilesystemAttachmentStore::path_for(
    const domain::AttachmentId& id) const {
  return root_ / (id.value() + ".bin");
}

application::Result<domain::Attachment> FilesystemAttachmentStore::put(
    const domain::NoteId& note_id, const std::string& file_name,
    const std::string& mime_type, const std::vector<std::uint8_t>& bytes) {
  const auto now = clock_.now_ms();
  domain::Attachment att;
  att.id = domain::AttachmentId{"att-" + std::to_string(now) + "-" +
                                 std::to_string(++seq_)};
  att.note_id = note_id;
  att.file_name = file_name;
  att.mime_type =
      mime_type.empty() ? "application/octet-stream" : mime_type;
  att.byte_size = static_cast<std::int64_t>(bytes.size());
  att.created_at_ms = now;

  const auto dest = path_for(att.id);
  const auto tmp = dest.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return application::Result<domain::Attachment>::fail(
          {application::ErrorKind::StorageFailure, "open temp failed"});
    }
    if (!bytes.empty()) {
      out.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    if (!out.good()) {
      return application::Result<domain::Attachment>::fail(
          {application::ErrorKind::StorageFailure, "write failed"});
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, dest, ec);
  if (ec) {
    std::filesystem::remove(tmp);
    return application::Result<domain::Attachment>::fail(
        {application::ErrorKind::StorageFailure, ec.message()});
  }
  return application::Result<domain::Attachment>::ok(std::move(att));
}

application::Result<std::vector<std::uint8_t>>
FilesystemAttachmentStore::get(const domain::AttachmentId& id) const {
  const auto p = path_for(id);
  if (!std::filesystem::exists(p)) {
    return application::Result<std::vector<std::uint8_t>>::fail(
        {application::ErrorKind::NotFound, "attachment not found"});
  }
  std::ifstream in(p, std::ios::binary);
  if (!in) {
    return application::Result<std::vector<std::uint8_t>>::fail(
        {application::ErrorKind::StorageFailure, "read failed"});
  }
  in.seekg(0, std::ios::end);
  const auto sz = in.tellg();
  in.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> data;
  if (sz > 0) {
    data.resize(static_cast<std::size_t>(sz));
    in.read(reinterpret_cast<char*>(data.data()), sz);
  }
  return application::Result<std::vector<std::uint8_t>>::ok(std::move(data));
}

application::Result<void> FilesystemAttachmentStore::remove(
    const domain::AttachmentId& id) {
  std::error_code ec;
  std::filesystem::remove(path_for(id), ec);
  return application::Result<void>::ok();
}

}  // namespace notes::adapters::attachments
