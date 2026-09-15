#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_content.hpp"

#include <cstddef>
#include <vector>

namespace notes::application {

class ToggleChecklistItem {
public:
  ToggleChecklistItem(NoteReader& reader, NoteWriter& writer, Clock& clock)
      : reader_(reader), writer_(writer), clock_(clock) {}

  struct Request {
    domain::NoteId note_id;
    std::size_t block_index{0};
    std::size_t item_index{0};
    std::int64_t base_revision{0};
  };

  [[nodiscard]] Result<domain::Note> execute(Request req) {
    auto loaded = reader_.load(req.note_id);
    if (!loaded) return loaded;
    auto note = std::move(loaded.value());
    if (note.revision != req.base_revision) {
      return Result<domain::Note>::fail({ErrorKind::RevisionConflict, "stale checklist toggle"});
    }
    auto blocks = note.content.blocks();
    if (req.block_index >= blocks.size()) {
      return Result<domain::Note>::fail({ErrorKind::ValidationFailed, "block index"});
    }
    auto* checklist = std::get_if<domain::ChecklistBlock>(&blocks[req.block_index]);
    if (!checklist) {
      return Result<domain::Note>::fail({ErrorKind::ValidationFailed, "not checklist block"});
    }
    blocks[req.block_index] = checklist->with_toggled(req.item_index);
    note.content = domain::NoteContent{std::move(blocks)};
    note.modified_at_ms = clock_.now_ms();
    return writer_.save(note);
  }

private:
  NoteReader& reader_;
  NoteWriter& writer_;
  Clock& clock_;
};

}  // namespace notes::application
