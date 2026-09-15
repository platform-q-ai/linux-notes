// Persistence roundtrip for structured note content (checklist + attachment refs).
// Does not depend on trash schema; uses current SqliteNoteStore + content codec.

#include "adapters/persistence/sqlite/content_codec.hpp"
#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"
#include "application/use_cases/notes/toggle_checklist_item.hpp"
#include "tests/support/require.hpp"
#include "tests/support/structured_fixtures.hpp"
#include "tests/support/temp_db.hpp"
#include "tests/support/fakes/fixed_clock.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

using notes::testing::require;

void test_codec_preserves_structure() {
  using namespace notes;
  const auto original = testing::make_mixed_structured_content("att-c1", "a.png");
  const auto blob = adapters::persistence::encode_content(original);
  const auto decoded = adapters::persistence::decode_content(blob);
  require(testing::content_has_checklist_and_attachment(decoded),
          "codec keeps checklist+attachment");
  require(decoded.blocks().size() == original.blocks().size(), "block count");
  // Attachment id identity
  bool att_ok = false;
  for (const auto& b : decoded.blocks()) {
    if (const auto* a = std::get_if<domain::AttachmentRefBlock>(&b)) {
      att_ok = a->attachment_id.value() == "att-c1" && a->display_name == "a.png";
    }
  }
  require(att_ok, "attachment identity");
}

void test_sqlite_save_reopen_structured() {
  using namespace notes;
  testing::TempDbPath path("structured-roundtrip.db");
  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(path.path())), "open db");
  adapters::persistence::SqliteNoteStore store(db);

  auto note = testing::make_structured_note("n-struct", "root");
  auto saved = store.save(note);
  require(saved.has_value(), "save structured");
  require(saved.value().revision == 1, "rev1");

  // Reopen DB handle to force durable read path.
  db.reset();
  db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(path.path())), "reopen");
  adapters::persistence::SqliteNoteStore store2(db);
  auto loaded = store2.load(domain::NoteId{std::string{"n-struct"}});
  require(loaded.has_value(), "load after reopen");
  require(testing::content_has_checklist_and_attachment(loaded.value().content),
          "structure survived reopen");
  require(loaded.value().title == "Structured", "title");
}

void test_toggle_checklist_preserves_neighbors() {
  using namespace notes;
  testing::TempDbPath path("structured-toggle.db");
  auto db = std::make_shared<adapters::persistence::SqliteDb>();
  require(static_cast<bool>(db->open(path.path())), "open");
  adapters::persistence::SqliteNoteStore store(db);
  testing::FixedClock clock{5000};

  auto note = testing::make_structured_note("n-tog", "root");
  auto saved = store.save(note);
  require(saved.has_value(), "seed");

  application::ToggleChecklistItem toggle{store, store, clock};
  application::ToggleChecklistItem::Request req;
  req.note_id = domain::NoteId{std::string{"n-tog"}};
  req.base_revision = saved.value().revision;
  req.item_index = 0;  // first checklist item
  // Find checklist block index
  std::size_t block_idx = 0;
  for (std::size_t i = 0; i < saved.value().content.blocks().size(); ++i) {
    if (std::holds_alternative<domain::ChecklistBlock>(
            saved.value().content.blocks()[i])) {
      block_idx = i;
      break;
    }
  }
  req.block_index = block_idx;
  auto toggled = toggle.execute(req);
  require(toggled.has_value(),
          toggled ? "toggle ok" : toggled.error().message.c_str());
  require(testing::content_has_checklist_and_attachment(toggled.value().content),
          "neighbors intact after toggle");
  require(testing::checklist_item_done_at(toggled.value().content, 0, true),
          "item 0 now done");
}

}  // namespace

int main() {
  try {
    test_codec_preserves_structure();
    test_sqlite_save_reopen_structured();
    test_toggle_checklist_preserves_neighbors();
    std::cerr << "structured_roundtrip_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "structured_roundtrip_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
