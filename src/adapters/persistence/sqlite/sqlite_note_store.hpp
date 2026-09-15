#pragma once

#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_searcher.hpp"
#include "application/ports/notes/note_writer.hpp"

#include <memory>

namespace notes::adapters::persistence {

// NoteReader + NoteWriter + NoteSearcher; CAS on note.revision as base.
class SqliteNoteStore final : public application::NoteReader,
                              public application::NoteWriter,
                              public application::NoteSearcher {
public:
  explicit SqliteNoteStore(std::shared_ptr<SqliteDb> db);

  [[nodiscard]] application::Result<domain::Note> load(
      const domain::NoteId& id) const override;

  [[nodiscard]] application::Result<std::vector<domain::NoteSummary>> list(
      const domain::FolderId& folder_id) const override;

  // CAS: existing.revision must equal note.revision; stored becomes rev+1.
  // Insert requires note.revision == 0 → stored revision 1.
  [[nodiscard]] application::Result<domain::Note> save(
      const domain::Note& note) override;

  [[nodiscard]] application::Result<void> remove(
      const domain::NoteId& id) override;

  [[nodiscard]] application::Result<std::vector<domain::NoteSummary>> search(
      const std::string& query) const override;

private:
  std::shared_ptr<SqliteDb> db_;
};

}  // namespace notes::adapters::persistence
