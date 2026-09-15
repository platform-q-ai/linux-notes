#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/purge_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/use_cases/notes/trash_note.hpp"
#include "domain/notes/note_content.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_attachment_store.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool cond, const char* msg) {
  if (!cond) {
    throw std::runtime_error(msg);
  }
}

}  // namespace

int main() {
  using namespace notes;
  try {
    testing::InMemoryNoteStore store;
    testing::FixedClock clock{50'000};
    testing::InMemoryAttachmentStore attachments{clock};

    // Seed folder via note store (in-memory accepts notes with folder ids).
    domain::Note seed;
    seed.id = domain::NoteId{"note-shared-att"};
    seed.folder_id = domain::FolderId{"root"};
    seed.title = "with att";
    seed.revision = 0;
    seed.created_at_ms = 1;
    seed.modified_at_ms = 1;

    auto put = attachments.put(seed.id, "photo.png", "image/png",
                               std::vector<std::uint8_t>{0x89, 0x50, 0x4e});
    require(static_cast<bool>(put), "put attachment");
    const auto original_att_id = put.value().id;

    domain::AttachmentRefBlock ref;
    ref.attachment_id = original_att_id;
    ref.display_name = "photo.png";
    seed.content =
        domain::NoteContent{std::vector<domain::ContentBlock>{ref}};
    require(static_cast<bool>(store.save(seed)), "save seed");

    // Concurrent head revision bumps CAS base.
    domain::Note head = store.load(seed.id).value();
    head.content = domain::NoteContent::from_plain_text("head won");
    head.revision = 1;
    // Keep the attachment on head too so both branches can matter; more
    // realistic: stale editor still has attachment, head replaced body.
    require(static_cast<bool>(store.save(head)), "save head");

    // Stale save with attachment content triggers keep-both.
    application::SaveNote save(store, store, clock, nullptr, &attachments);
    application::SaveNote::Request req;
    req.note = seed;  // revision 0 stale vs store rev 1... wait head saved as rev1
    // After first save seed rev becomes 1 from store.save; head load had rev1 and
    // save bumps to 2. Reload carefully:
    // Re-setup for clarity:
  } catch (const std::exception&) {
    // fall through to clean setup below
  }

  try {
    testing::InMemoryNoteStore store;
    testing::FixedClock clock{50'000};
    testing::InMemoryAttachmentStore attachments{clock};

    domain::Note base;
    base.id = domain::NoteId{"note-shared-att"};
    base.folder_id = domain::FolderId{"root"};
    base.title = "with att";
    base.revision = 0;
    base.created_at_ms = 1;
    base.modified_at_ms = 1;

    auto put = attachments.put(base.id, "photo.png", "image/png",
                               std::vector<std::uint8_t>{0x89, 0x50, 0x4e});
    require(static_cast<bool>(put), "put");
    const auto original_att_id = put.value().id;
    domain::AttachmentRefBlock ref;
    ref.attachment_id = original_att_id;
    ref.display_name = "photo.png";
    base.content =
        domain::NoteContent{std::vector<domain::ContentBlock>{ref}};
    require(static_cast<bool>(store.save(base)), "save base");  // rev -> 1

    // Concurrent writer wins next revision without touching attachment store.
    domain::Note concurrent = store.load(base.id).value();
    concurrent.title = "head title";
    concurrent.content = domain::NoteContent{std::vector<domain::ContentBlock>{
        ref, domain::ParagraphBlock{
                 std::vector<domain::TextSpan>{domain::TextSpan{"head"}}}}};
    require(static_cast<bool>(store.save(concurrent)), "concurrent");  // rev 2

    // Stale client still at rev 1 with attachment-only content → keep-both.
    application::SaveNote save(store, store, clock, nullptr, &attachments);
    application::SaveNote::Request req;
    req.note = base;
    req.note.revision = 1;
    req.note.title = "stale edit";
    req.note.content =
        domain::NoteContent{std::vector<domain::ContentBlock>{ref}};
    req.allow_keep_both = true;
    auto out = save.execute(std::move(req));
    require(out.has_value() && out.value().kept_both, "keep-both ok");
    const auto conflict_id = out.value().saved.id;
    require(conflict_id != base.id, "new id");

    // Conflict note must reference a *distinct* attachment blob id.
    const auto conflict = store.load(conflict_id).value();
    const auto* att_block =
        std::get_if<domain::AttachmentRefBlock>(&conflict.content.blocks().at(0));
    require(att_block != nullptr, "conflict has attachment block");
    const auto conflict_att_id = att_block->attachment_id;
    require(conflict_att_id != original_att_id,
            "keep-both deep-copied attachment id (not shared)");
    auto conflict_bytes = attachments.get(conflict_att_id);
    require(static_cast<bool>(conflict_bytes) &&
                conflict_bytes.value() ==
                    std::vector<std::uint8_t>({0x89, 0x50, 0x4e}),
            "conflict blob bytes intact");
    auto prior_bytes = attachments.get(original_att_id);
    require(static_cast<bool>(prior_bytes), "prior blob still present");

    // Purge conflict copy must not delete survivor (prior) blob.
    application::TrashNote trash{store, clock};
    require(static_cast<bool>(trash.execute(conflict_id)), "trash conflict");
    application::PurgeNote purge{store, store, &attachments};
    require(static_cast<bool>(purge.execute(conflict_id)), "purge conflict");
    require(!static_cast<bool>(store.load(conflict_id)), "conflict gone");
    auto survivor_blob = attachments.get(original_att_id);
    require(static_cast<bool>(survivor_blob),
            "prior attachment blob survives purge of conflict copy");
    auto purged_conflict_blob = attachments.get(conflict_att_id);
    require(!purged_conflict_blob,
            "conflict-owned deep-copy blob is GC'd on purge");

    // Prior note still references original id.
    const auto prior = store.load(base.id).value();
    bool prior_has_original = false;
    for (const auto& b : prior.content.blocks()) {
      if (const auto* a = std::get_if<domain::AttachmentRefBlock>(&b)) {
        if (a->attachment_id == original_att_id) {
          prior_has_original = true;
        }
      }
    }
    require(prior_has_original, "prior still refs original att id");

    std::cout << "keep_both_attachment_test PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "FAIL " << ex.what() << "\n";
    return 1;
  }
}
