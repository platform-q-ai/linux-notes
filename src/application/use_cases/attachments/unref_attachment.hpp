#pragma once
#include "application/ports/attachments/attachment_store.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/result.hpp"
#include "domain/attachments/attachment_id.hpp"
#include "domain/notes/note_content.hpp"
#include "domain/notes/note_id.hpp"

#include <string>
#include <utility>
#include <variant>

namespace notes::application {

// Shared-id attachment GC: delete blob bytes only when no remaining note
// (active or trashed) still references the opaque-safe id. Cross-note paste /
// degraded keep-both without deep-copy can share ids; unref-only delete
// prevents survivor data loss.
class UnrefAttachment {
public:
  UnrefAttachment(NoteReader& reader, AttachmentStore& attachments)
      : reader_(reader), attachments_(attachments) {}

  // exclude_note_id: the note that just dropped the ref (or was purged) so its
  // still-loaded content does not count as a live reference.
  [[nodiscard]] Result<void> execute(const domain::AttachmentId& attachment_id,
                                     const domain::NoteId& exclude_note_id) {
    if (attachment_id.empty()) {
      return Result<void>::fail(
          {ErrorKind::ValidationFailed, "attachment id required"});
    }
    if (!attachment_id.is_opaque_safe()) {
      return Result<void>::fail(
          {ErrorKind::ValidationFailed, "attachment id not opaque-safe"});
    }

    auto ids = reader_.all_note_ids();
    if (!ids) {
      return Result<void>::fail(ids.error());
    }

    const std::string& aid = attachment_id.value();
    for (const auto& note_id : ids.value()) {
      if (!exclude_note_id.empty() && note_id == exclude_note_id) {
        continue;
      }
      auto loaded = reader_.load(note_id);
      if (!loaded) {
        // Race: note vanished between all_note_ids and load — ignore.
        continue;
      }
      if (content_references_attachment(loaded.value().content, aid)) {
        // Still referenced elsewhere — keep blob.
        return Result<void>::ok();
      }
    }

    return attachments_.remove(attachment_id);
  }

  [[nodiscard]] static bool content_references_attachment(
      const domain::NoteContent& content, const std::string& attachment_id) {
    for (const auto& block : content.blocks()) {
      if (const auto* a = std::get_if<domain::AttachmentRefBlock>(&block)) {
        if (a->attachment_id.value() == attachment_id) {
          return true;
        }
      }
    }
    return false;
  }

private:
  NoteReader& reader_;
  AttachmentStore& attachments_;
};

}  // namespace notes::application
