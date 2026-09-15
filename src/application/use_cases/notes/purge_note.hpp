#pragma once
#include "application/ports/attachments/attachment_store.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/notes/note_content.hpp"
#include "domain/notes/note_id.hpp"

#include <set>
#include <string>

namespace notes::application {

// Permanent delete: requires note already trashed (deliberate two-step).
// Removes attachment blobs referenced by the note content when store provided.
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
    // Collect attachment ids before row is gone.
    std::set<std::string> attachment_ids;
    for (const auto& block : loaded.value().content.blocks()) {
      if (const auto* a = std::get_if<domain::AttachmentRefBlock>(&block)) {
        if (!a->attachment_id.empty()) {
          attachment_ids.insert(a->attachment_id.value());
        }
      }
    }
    auto removed = writer_.remove(id);
    if (!removed) return removed;
    if (attachments_ != nullptr) {
      for (const auto& aid : attachment_ids) {
        // Best-effort GC: note row is already gone; log via ignored failures.
        (void)attachments_->remove(domain::AttachmentId{aid});
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
