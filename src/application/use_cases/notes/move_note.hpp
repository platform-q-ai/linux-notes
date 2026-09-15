#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/folders/folder_reader.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/folders/folder_id.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_id.hpp"

namespace notes::application {

// Move an active (non-trashed) note between folders. CAS via note.revision.
class MoveNote {
public:
  MoveNote(NoteReader& reader, NoteWriter& writer, FolderReader& folders,
           Clock& clock)
      : reader_(reader), writer_(writer), folders_(folders), clock_(clock) {}

  struct Request {
    domain::NoteId note_id;
    domain::FolderId target_folder_id;
  };

  [[nodiscard]] Result<domain::Note> execute(Request req) {
    if (req.note_id.empty()) {
      return Result<domain::Note>::fail(
          {ErrorKind::ValidationFailed, "note id required"});
    }
    if (req.target_folder_id.empty()) {
      return Result<domain::Note>::fail(
          {ErrorKind::ValidationFailed, "target folder required"});
    }
    auto folder = folders_.load(req.target_folder_id);
    if (!folder) {
      return Result<domain::Note>::fail(
          {ErrorKind::NotFound, "target folder not found"});
    }
    auto loaded = reader_.load(req.note_id);
    if (!loaded) return loaded;
    auto note = std::move(loaded.value());
    if (note.is_trashed()) {
      return Result<domain::Note>::fail(
          {ErrorKind::ValidationFailed, "cannot move a trashed note; restore first"});
    }
    if (note.folder_id == req.target_folder_id) {
      return Result<domain::Note>::ok(std::move(note));
    }
    note.folder_id = std::move(req.target_folder_id);
    note.modified_at_ms = clock_.now_ms();
    return writer_.save(note);
  }

private:
  NoteReader& reader_;
  NoteWriter& writer_;
  FolderReader& folders_;
  Clock& clock_;
};

}  // namespace notes::application
