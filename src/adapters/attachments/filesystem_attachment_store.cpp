#include "adapters/attachments/filesystem_attachment_store.hpp"

#include <fstream>
#include <system_error>

namespace notes::adapters::attachments {
namespace {

bool path_is_strictly_under(const std::filesystem::path& root,
                            const std::filesystem::path& candidate) {
  const auto root_n = root.lexically_normal();
  const auto cand_n = candidate.lexically_normal();
  auto root_it = root_n.begin();
  auto cand_it = cand_n.begin();
  for (; root_it != root_n.end() && cand_it != cand_n.end();
       ++root_it, ++cand_it) {
    if (*root_it != *cand_it) {
      return false;
    }
  }
  // candidate must be a proper descendant (not equal to root).
  return root_it == root_n.end() && cand_it != cand_n.end();
}

std::filesystem::path normalized_absolute(const std::filesystem::path& p) {
  std::error_code ec;
  auto abs = std::filesystem::absolute(p, ec);
  if (ec) {
    abs = p;
  }
  return abs.lexically_normal();
}

}  // namespace

FilesystemAttachmentStore::FilesystemAttachmentStore(
    std::filesystem::path root, application::Clock& clock)
    : root_(std::move(root)), clock_(clock) {
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
}

application::Result<std::filesystem::path>
FilesystemAttachmentStore::contained_path(
    const domain::AttachmentId& id) const {
  // FS seam for untrusted AttachmentId values (markers, anchors, purge GC):
  // 1) domain opaque-safe token (rejects separators/traversal/absolute/empty)
  // 2) lexical join under root_ must stay a strict descendant
  // Callers (get) additionally refuse symlink/non-regular reads so a planted
  // symlink under root cannot disclose an external target.
  if (!id.is_opaque_safe()) {
    return application::Result<std::filesystem::path>::fail(
        {application::ErrorKind::ValidationFailed,
         "attachment id is not an opaque safe token"});
  }

  const auto root_base = normalized_absolute(root_);
  const auto joined =
      normalized_absolute(root_ / (id.value() + ".bin"));
  if (!path_is_strictly_under(root_base, joined)) {
    return application::Result<std::filesystem::path>::fail(
        {application::ErrorKind::ValidationFailed,
         "attachment path escapes store root"});
  }
  return application::Result<std::filesystem::path>::ok(joined);
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

  auto dest_r = contained_path(att.id);
  if (!dest_r) {
    return application::Result<domain::Attachment>::fail(dest_r.error());
  }
  const auto dest = dest_r.value();
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
  auto p_r = contained_path(id);
  if (!p_r) {
    return application::Result<std::vector<std::uint8_t>>::fail(p_r.error());
  }
  const auto& p = p_r.value();
  std::error_code ec;
  // Symlink containment: never read through a symlink. A symlink entry under
  // root could point at arbitrary external files; treat as invalid blob.
  if (std::filesystem::is_symlink(p, ec)) {
    return application::Result<std::vector<std::uint8_t>>::fail(
        {application::ErrorKind::ValidationFailed,
         "attachment symlink refused"});
  }
  if (!std::filesystem::is_regular_file(p, ec)) {
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
  auto p_r = contained_path(id);
  if (!p_r) {
    return application::Result<void>::fail(p_r.error());
  }
  const auto& p = p_r.value();
  std::error_code ec;
  // Unlink only the directory entry under root. For a symlink this removes the
  // link inode inside the store and does not delete an external target.
  if (!std::filesystem::exists(p, ec) && !std::filesystem::is_symlink(p, ec)) {
    return application::Result<void>::ok();
  }
  if (std::filesystem::is_symlink(p, ec) ||
      std::filesystem::is_regular_file(p, ec)) {
    std::filesystem::remove(p, ec);
    return application::Result<void>::ok();
  }
  return application::Result<void>::fail(
      {application::ErrorKind::ValidationFailed,
       "refusing to remove non-regular attachment path"});
}

}  // namespace notes::adapters::attachments
