#pragma once
#include "application/ports/attachments/attachment_store.hpp"
#include "application/ports/clock.hpp"
#include "application/ports/id_source.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "application/result.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_content.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace notes::application {

// Application owns timestamps, revision bump, and keep-both on RevisionConflict.
//
// Attachment lifetime policy (keep-both):
// When allow_keep_both forks a note that references attachment blobs, those
// AttachmentRefBlock ids are deep-copied through AttachmentStore (get + put)
// onto the new note. Each note therefore owns distinct blob ids. PurgeNote and
// removeAttachment may delete blobs referenced by the note being removed
// without destroying the survivor's attachments. This is preferred over a
// durable refcount table (out of scope) and over "never delete" retention.
// If AttachmentStore is not wired, keep-both still forks the note row but
// attachment ids remain shared (test-only / degraded); production composition
// always supplies the store.
class SaveNote {
public:
  // id_source optional: when null, a process CSPRNG token is used (restart/multi-
  // instance safe). Inject a custom IdSource for tests or host-provided identity.
  // attachments optional: when non-null, keep-both deep-copies attachment blobs.
  SaveNote(NoteWriter& writer, NoteReader& reader, Clock& clock,
           IdSource* id_source = nullptr,
           AttachmentStore* attachments = nullptr)
      : writer_(writer),
        reader_(reader),
        clock_(clock),
        id_source_(id_source),
        attachments_(attachments) {}

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

    if (auto deep = deep_copy_attachments(incoming); !deep) {
      return Result<Outcome>::fail(deep.error());
    }

    auto created = writer_.save(incoming);
    if (!created) {
      // Best-effort: leave any newly put blobs; purge/orphan GC can reclaim later.
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
  // Rewrite AttachmentRefBlock ids on `note` to freshly put blobs cloned from
  // the previous ids. No-op when attachments_ is null or content has no refs.
  [[nodiscard]] Result<void> deep_copy_attachments(domain::Note& note) {
    if (attachments_ == nullptr) {
      return Result<void>::ok();
    }
    auto blocks = note.content.blocks();
    bool any = false;
    for (auto& block : blocks) {
      auto* ref = std::get_if<domain::AttachmentRefBlock>(&block);
      if (ref == nullptr || ref->attachment_id.empty()) {
        continue;
      }
      if (!ref->attachment_id.is_opaque_safe()) {
        // Skip unsafe tokens; do not attempt FS clone.
        continue;
      }
      auto bytes = attachments_->get(ref->attachment_id);
      if (!bytes) {
        // Missing source blob: drop the ref rather than share a dead id.
        // Keep display name as plain paragraph so content is not silent-empty.
        domain::ParagraphBlock para;
        para.spans.push_back(domain::TextSpan{
            ref->display_name.empty() ? std::string{"[missing attachment]"}
                                      : ref->display_name,
            false, false, false});
        block = std::move(para);
        any = true;
        continue;
      }
      const std::string name =
          ref->display_name.empty() ? std::string{"attachment.bin"}
                                    : ref->display_name;
      auto put = attachments_->put(note.id, name, "application/octet-stream",
                                   bytes.value());
      if (!put) {
        return Result<void>::fail(put.error());
      }
      ref->attachment_id = put.value().id;
      if (ref->display_name.empty()) {
        ref->display_name = put.value().file_name;
      }
      any = true;
    }
    if (any) {
      note.content = domain::NoteContent{std::move(blocks)};
    }
    return Result<void>::ok();
  }

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
  AttachmentStore* attachments_{nullptr};
};

}  // namespace notes::application
