#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/notes/note.hpp"

#include <string>
#include <utility>

namespace notes::application {

// Application owns timestamps, revision bump, and keep-both on RevisionConflict.
class SaveNote {
public:
  SaveNote(NoteWriter& writer, NoteReader& reader, Clock& clock)
      : writer_(writer), reader_(reader), clock_(clock) {}

  struct Request {
    domain::Note note;           // includes base revision
    bool allow_keep_both{true};  // on conflict, preserve prior as new note
  };

  struct Outcome {
    domain::Note saved;
    bool kept_both{false};
    domain::NoteId preserved_prior_id{};
  };

  [[nodiscard]] Result<Outcome> execute(Request req) {
    if (req.note.id.empty()) {
      return Result<Outcome>::fail({ErrorKind::ValidationFailed, "note id required"});
    }
    req.note.modified_at_ms = clock_.now_ms();
    auto save_result = writer_.save(req.note);
    if (save_result) {
      Outcome out;
      out.saved = std::move(save_result.value());
      return Result<Outcome>::ok(std::move(out));
    }
    if (save_result.error().kind != ErrorKind::RevisionConflict || !req.allow_keep_both) {
      return Result<Outcome>::fail(save_result.error());
    }
    // keep-both: load prior (current store version), save incoming as new note id
    auto prior = reader_.load(req.note.id);
    if (!prior) {
      return Result<Outcome>::fail(prior.error());
    }
    domain::Note incoming = std::move(req.note);
    const domain::NoteId original_id = incoming.id;
    incoming.id = domain::NoteId{"note-conflict-" + std::to_string(clock_.now_ms())};
    incoming.revision = 0;
    incoming.created_at_ms = clock_.now_ms();
    incoming.modified_at_ms = incoming.created_at_ms;
    if (incoming.title.find("(conflict)") == std::string::npos) {
      incoming.title += " (conflict)";
    }
    auto created = writer_.save(incoming);
    if (!created) {
      return Result<Outcome>::fail(created.error());
    }
    Outcome out;
    out.saved = std::move(created.value());
    out.kept_both = true;
    out.preserved_prior_id = original_id;
    (void)prior;
    return Result<Outcome>::ok(std::move(out));
  }

private:
  NoteWriter& writer_;
  NoteReader& reader_;
  Clock& clock_;
};

}  // namespace notes::application
