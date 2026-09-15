#pragma once
#include "application/ports/folders/folder_reader.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/folders/folder_id.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_id.hpp"

namespace notes::application {

// Restore a trashed note to its prior folder, or fallback_folder if that is gone.
class RestoreNote {
public:
  RestoreNote(NoteReader& reader, NoteWriter& writer, FolderReader& folders,
              domain::FolderId fallback_folder = domain::FolderId{"root"})
      : reader_(reader),
        writer_(writer),
        folders_(folders),
        fallback_(std::move(fallback_folder)) {}

  [[nodiscard]] Result<domain::Note> execute(const domain::NoteId& id) {
    if (id.empty()) {
      return Result<domain::Note>::fail(
          {ErrorKind::ValidationFailed, "note id required"});
    }
    auto loaded = reader_.load(id);
    if (!loaded) return loaded;
    const auto& note = loaded.value();
    if (!note.is_trashed()) {
      return Result<domain::Note>::fail(
          {ErrorKind::ValidationFailed, "note is not in trash"});
    }
    domain::FolderId target = fallback_;
    if (note.trashed_from_folder_id && !note.trashed_from_folder_id->empty()) {
      auto folder = folders_.load(*note.trashed_from_folder_id);
      if (folder) {
        target = *note.trashed_from_folder_id;
      }
    } else if (!note.folder_id.empty()) {
      auto folder = folders_.load(note.folder_id);
      if (folder) target = note.folder_id;
    }
    return writer_.restore(id, target);
  }

private:
  NoteReader& reader_;
  NoteWriter& writer_;
  FolderReader& folders_;
  domain::FolderId fallback_;
};

}  // namespace notes::application
