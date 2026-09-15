#pragma once
#include "application/ports/attachments/attachment_store.hpp"
#include "application/ports/clock.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/attachments/attachment.hpp"
#include "domain/notes/note_content.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace notes::application {

class AttachFile {
public:
  AttachFile(AttachmentStore& store, NoteReader& reader, NoteWriter& writer, Clock& clock)
      : store_(store), reader_(reader), writer_(writer), clock_(clock) {}

  struct Request {
    domain::NoteId note_id;
    std::string file_name;
    std::string mime_type;
    std::vector<std::uint8_t> bytes;
    std::int64_t base_revision{0};
  };

  struct Outcome {
    domain::Attachment attachment;
    domain::Note note;
  };

  [[nodiscard]] Result<Outcome> execute(Request req) {
    if (req.note_id.empty() || req.file_name.empty()) {
      return Result<Outcome>::fail({ErrorKind::ValidationFailed, "note and file name required"});
    }
    auto loaded = reader_.load(req.note_id);
    if (!loaded) return Result<Outcome>::fail(loaded.error());
    auto note = std::move(loaded.value());
    if (note.revision != req.base_revision) {
      return Result<Outcome>::fail({ErrorKind::RevisionConflict, "stale attach"});
    }
    auto put = store_.put(req.note_id, req.file_name, req.mime_type, req.bytes);
    if (!put) return Result<Outcome>::fail(put.error());
    auto blocks = note.content.blocks();
    domain::AttachmentRefBlock ref;
    ref.attachment_id = put.value().id;
    ref.display_name = put.value().file_name;
    blocks.push_back(std::move(ref));
    note.content = domain::NoteContent{std::move(blocks)};
    note.modified_at_ms = clock_.now_ms();
    auto saved = writer_.save(note);
    if (!saved) {
      (void)store_.remove(put.value().id);
      return Result<Outcome>::fail(saved.error());
    }
    Outcome out;
    out.attachment = std::move(put.value());
    out.note = std::move(saved.value());
    return Result<Outcome>::ok(std::move(out));
  }

private:
  AttachmentStore& store_;
  NoteReader& reader_;
  NoteWriter& writer_;
  Clock& clock_;
};

}  // namespace notes::application
