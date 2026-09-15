#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/folders/folder_id.hpp"
#include "domain/notes/note.hpp"

#include <string>
#include <utility>

namespace notes::application {

class CreateNote {
public:
  CreateNote(NoteWriter& writer, Clock& clock) : writer_(writer), clock_(clock) {}

  struct Request {
    domain::FolderId folder_id;
    std::string title;
    domain::NoteContent content{};
  };

  [[nodiscard]] Result<domain::Note> execute(Request req) {
    if (req.folder_id.empty()) {
      return Result<domain::Note>::fail({ErrorKind::ValidationFailed, "folder_id required"});
    }
    const auto now = clock_.now_ms();
    domain::Note note;
    note.id = domain::NoteId{make_id(now)};
    note.folder_id = std::move(req.folder_id);
    note.title = std::move(req.title);
    note.content = std::move(req.content);
    note.created_at_ms = now;
    note.modified_at_ms = now;
    note.revision = 0;  // base for first save
    note.pinned = false;
    return writer_.save(note);
  }

private:
  static std::string make_id(std::int64_t now) {
    return "note-" + std::to_string(now) + "-" + std::to_string(++seq_);
  }
  static inline std::uint64_t seq_{0};
  NoteWriter& writer_;
  Clock& clock_;
};

}  // namespace notes::application
