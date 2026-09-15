#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/id_source.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/notes/note.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <utility>

namespace notes::application {

// Application owns timestamps, revision bump, and keep-both on RevisionConflict.
class SaveNote {
public:
  // id_source optional: when null, a process CSPRNG token is used (restart/multi-
  // instance safe). Inject a custom IdSource for tests or host-provided identity.
  SaveNote(NoteWriter& writer, NoteReader& reader, Clock& clock,
           IdSource* id_source = nullptr)
      : writer_(writer), reader_(reader), clock_(clock), id_source_(id_source) {}

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
    incoming.id = domain::NoteId{make_conflict_id(clock_.now_ms())};
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
  [[nodiscard]] std::string next_token() {
    if (id_source_ != nullptr) {
      return id_source_->next_unique_token();
    }
    return default_random_token();
  }

  static std::string default_random_token() {
    // Mix random_device entropy with a process-boot salt so two instances at the
    // same clock tick (and after "restart" of static state) still diverge.
    // seed_seq must be an lvalue — mt19937_64 ctor takes non-const seed_seq&.
    thread_local std::random_device rd;
    thread_local const std::uint64_t boot_salt =
        (static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd());
    thread_local std::seed_seq seed{
        static_cast<std::uint32_t>(boot_salt),
        static_cast<std::uint32_t>(boot_salt >> 32), rd(), rd()};
    thread_local std::mt19937_64 gen{seed};
    std::array<std::uint8_t, 16> bytes{};
    for (auto& b : bytes) {
      b = static_cast<std::uint8_t>(gen() & 0xffu);
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      out[i * 2] = kHex[(bytes[i] >> 4) & 0xf];
      out[i * 2 + 1] = kHex[bytes[i] & 0xf];
    }
    return out;
  }

  std::string make_conflict_id(std::int64_t now_ms) {
    // Format: note-conflict-{ms}-{token}. Token is not a process-local counter;
    // it is entropy (or injected identity), so restarts/multi-instance at the
    // same ms do not collide.
    return "note-conflict-" + std::to_string(now_ms) + "-" + next_token();
  }

  NoteWriter& writer_;
  NoteReader& reader_;
  Clock& clock_;
  IdSource* id_source_{nullptr};
};

}  // namespace notes::application
