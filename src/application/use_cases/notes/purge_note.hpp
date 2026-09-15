#pragma once
#include "application/ports/attachments/attachment_store.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "application/use_cases/attachments/unref_attachment.hpp"
#include "domain/notes/note_content.hpp"
#include "domain/notes/note_id.hpp"

#include <set>
#include <string>

namespace notes::application {

// Permanent delete: requires note already trashed (deliberate two-step).
// Attachment GC is unref-only: blobs still referenced by other notes survive.
class PurgeNote {
public:
  PurgeNote(NoteReader& reader, NoteWriter& writer,
            AttachmentStore* attachments = nullptr)
      : reader_(reader), writer_(writer), attachments_(attachments) {}

  [[nodiscard]] Result<void> execute(const domain::NoteId& id) {
    if (id.empty()) {
      return Result<void>::fail({ErrorKind::ValidationFailed, "note id required"});
    }
    auto loaded = reader_.load(id);
    if (!loaded) return Result<void>::fail(loaded.error());
    if (!loaded.value().is_trashed()) {
      return Result<void>::fail(
          {ErrorKind::ValidationFailed,
           "permanent delete requires note to be in trash first"});
    }
    // Collect attachment ids before row is gone. Only opaque-safe tokens are
    // eligible for FS GC — forged/traversal ids must never reach the store.
    std::set<std::string> attachment_ids;
    for (const auto& block : loaded.value().content.blocks()) {
      if (const auto* a = std::get_if<domain::AttachmentRefBlock>(&block)) {
        if (a->attachment_id.is_opaque_safe()) {
          attachment_ids.insert(a->attachment_id.value());
        }
      }
    }
    auto removed = writer_.remove(id);
    if (!removed) return removed;
    if (attachments_ != nullptr) {
      UnrefAttachment unref{reader_, *attachments_};
      for (const auto& aid : attachment_ids) {
        // Best-effort unref GC: note row is already gone (exclude_note_id=id).
        (void)unref.execute(domain::AttachmentId{aid}, id);
      }
    }
    return Result<void>::ok();
  }

private:
  NoteReader& reader_;
  NoteWriter& writer_;
  AttachmentStore* attachments_;
};

}  // namespace notes::application
