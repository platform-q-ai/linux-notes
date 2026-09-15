// F5: shared attachment id — remove/purge one note must not delete survivor bytes.
#include "application/use_cases/attachments/unref_attachment.hpp"
#include "application/use_cases/notes/purge_note.hpp"
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
    testing::FixedClock clock{60'000};
    testing::InMemoryAttachmentStore attachments{clock};

    domain::Folder root;
    root.id = domain::FolderId{"root"};
    root.name = "Notes";
    require(static_cast<bool>(store.save(root)), "root");

    const std::vector<std::uint8_t> bytes{0xde, 0xad, 0xbe, 0xef, 0x01};

    domain::Note owner;
    owner.id = domain::NoteId{"note-owner"};
    owner.folder_id = root.id;
    owner.title = "owner";
    owner.revision = 0;
    owner.created_at_ms = 1;
    owner.modified_at_ms = 1;

    auto put = attachments.put(owner.id, "shared.bin", "application/octet-stream",
                               bytes);
    require(static_cast<bool>(put), "put");
    const auto shared_id = put.value().id;
    require(shared_id.is_opaque_safe(), "opaque-safe id");

    domain::AttachmentRefBlock ref;
    ref.attachment_id = shared_id;
    ref.display_name = "shared.bin";
    owner.content =
        domain::NoteContent{std::vector<domain::ContentBlock>{ref}};
    require(static_cast<bool>(store.save(owner)), "save owner");

    // Second note adopts the same opaque-safe id (cross-note paste / degraded path).
    domain::Note survivor;
    survivor.id = domain::NoteId{"note-survivor"};
    survivor.folder_id = root.id;
    survivor.title = "survivor";
    survivor.revision = 0;
    survivor.created_at_ms = 2;
    survivor.modified_at_ms = 2;
    survivor.content =
        domain::NoteContent{std::vector<domain::ContentBlock>{ref}};
    require(static_cast<bool>(store.save(survivor)), "save survivor");

    // --- Path A: unref after owner drops ref (simulates removeAttachment GC) ---
    {
      domain::Note dropped = store.load(owner.id).value();
      dropped.content = domain::NoteContent::from_plain_text("no att");
      dropped.revision = store.load(owner.id).value().revision;
      require(static_cast<bool>(store.save(dropped)), "owner drop ref");

      // Baseline bug: attachments.remove(shared_id) would kill survivor bytes.
      // Correct: UnrefAttachment keeps blob while survivor still references it.
      application::UnrefAttachment unref{store, attachments};
      require(static_cast<bool>(unref.execute(shared_id, owner.id)),
              "unref after drop");
      auto still = attachments.get(shared_id);
      require(static_cast<bool>(still), "survivor bytes after unref");
      require(still.value() == bytes, "bytes match after unref");
    }

    // --- Path B: purge owner note with shared id still on survivor ---
    {
      // Re-attach on owner so purge collects the shared id, then trash+purge.
      domain::Note re = store.load(owner.id).value();
      re.content = domain::NoteContent{std::vector<domain::ContentBlock>{ref}};
      re.revision = store.load(owner.id).value().revision;
      require(static_cast<bool>(store.save(re)), "owner re-ref");

      application::TrashNote trash{store, clock};
      require(static_cast<bool>(trash.execute(owner.id)), "trash owner");
      application::PurgeNote purge{store, store, &attachments};
      require(static_cast<bool>(purge.execute(owner.id)), "purge owner");
      require(!static_cast<bool>(store.load(owner.id)), "owner gone");

      auto still = attachments.get(shared_id);
      require(static_cast<bool>(still), "survivor bytes after purge");
      require(still.value() == bytes, "bytes match after purge");

      // Survivor still loads and can get bytes.
      auto surv = store.load(survivor.id);
      require(static_cast<bool>(surv), "survivor note");
      const auto* a = std::get_if<domain::AttachmentRefBlock>(
          &surv.value().content.blocks().at(0));
      require(a != nullptr, "survivor still has ref");
      require(a->attachment_id == shared_id, "same id");
    }

    // --- Path C: last reference may delete ---
    {
      application::UnrefAttachment unref{store, attachments};
      // Drop survivor ref via note rewrite then unref.
      domain::Note s = store.load(survivor.id).value();
      s.content = domain::NoteContent::from_plain_text("cleared");
      s.revision = store.load(survivor.id).value().revision;
      require(static_cast<bool>(store.save(s)), "survivor drop");
      require(static_cast<bool>(unref.execute(shared_id, survivor.id)),
              "unref last");
      auto gone = attachments.get(shared_id);
      require(!gone && gone.error().kind == application::ErrorKind::NotFound,
              "blob GC when unrefe'd");
    }

    std::cout << "shared_attachment_survivor_test PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "shared_attachment_survivor_test FAIL: " << ex.what() << "\n";
    return 1;
  }
}
