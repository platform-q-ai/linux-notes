#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

#include "application/use_cases/notes/load_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "application/use_cases/notes/toggle_checklist_item.hpp"
#include "domain/notes/checklist.hpp"
#include "domain/notes/note_content.hpp"
#include "presentation/qt/mapping/note_content_document_mapper.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <QCoreApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTest>

#include <string>
#include <variant>
#include <vector>

namespace {

void pump(int rounds = 40) {
  for (int i = 0; i < rounds; ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(10);
  }
}

notes::domain::NoteContent content_with_attachment_between_checks() {
  using namespace notes::domain;
  ChecklistBlock first{std::vector<ChecklistItem>{
      ChecklistItem{false, "alpha"},
      ChecklistItem{false, std::string("beta-\xce\xb2")},  // non-ASCII
  }};
  AttachmentRefBlock att;
  att.attachment_id = AttachmentId{"att-1234-9"};
  att.display_name = "photo.png";
  ChecklistBlock second{std::vector<ChecklistItem>{
      ChecklistItem{false, "gamma"},
  }};
  ParagraphBlock rich;
  rich.spans.push_back(TextSpan{"Bold", true, false, false});
  rich.spans.push_back(TextSpan{"Tail", false, false, false});
  return NoteContent{std::vector<ContentBlock>{first, att, second, rich}};
}

}  // namespace

TEST_CASE("plain_text offset walker hits checklist after attachment marker",
          "[editor][checklist][plain][p1]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{50'000};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::domain::Note seed;
  seed.id = notes::domain::NoteId{"note-check-1"};
  seed.folder_id = folder.id;
  seed.title = "c";
  seed.content = content_with_attachment_between_checks();
  seed.revision = 0;
  seed.created_at_ms = 1;
  seed.modified_at_ms = 1;
  REQUIRE(store.save(seed));

  notes::application::LoadNote load{store};
  notes::application::SaveNote save{store, store, clock};
  notes::application::ToggleChecklistItem toggle{store, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher, &toggle};

  editor.openNote(QString::fromStdString(seed.id.value()));
  pump();

  const std::string plain = seed.content.plain_text();
  // Offset of "gamma" line: find "[ ] gamma" in plain_text.
  const auto gamma_pos = plain.find("[ ] gamma");
  REQUIRE(gamma_pos != std::string::npos);

  REQUIRE(editor.toggleChecklistAtPlainOffset(static_cast<int>(gamma_pos)));
  pump();

  auto loaded = store.load(seed.id);
  REQUIRE(loaded);
  const auto& blocks = loaded.value().content.blocks();
  REQUIRE(blocks.size() >= 3);
  const auto* second = std::get_if<notes::domain::ChecklistBlock>(&blocks[2]);
  REQUIRE(second != nullptr);
  REQUIRE(second->items().at(0).done);
  // First checklist untouched.
  const auto* first = std::get_if<notes::domain::ChecklistBlock>(&blocks[0]);
  REQUIRE(first != nullptr);
  REQUIRE_FALSE(first->items().at(0).done);
}

TEST_CASE("document position API toggles correct QTextBlock checklist row",
          "[editor][checklist][qml-coords][p1]") {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{51'000};
  notes::domain::Folder folder;
  folder.id = notes::domain::FolderId{"f"};
  folder.name = "N";
  REQUIRE(store.save(folder));

  notes::domain::Note seed;
  seed.id = notes::domain::NoteId{"note-check-doc"};
  seed.folder_id = folder.id;
  seed.title = "c";
  seed.content = content_with_attachment_between_checks();
  seed.revision = 0;
  seed.created_at_ms = 1;
  seed.modified_at_ms = 1;
  REQUIRE(store.save(seed));

  notes::application::LoadNote load{store};
  notes::application::SaveNote save{store, store, clock};
  notes::application::ToggleChecklistItem toggle{store, store, clock};
  notes::presentation::UseCaseDispatcher dispatcher;
  notes::presentation::EditorViewModel editor{load, save, dispatcher, &toggle};

  editor.openNote(QString::fromStdString(seed.id.value()));
  pump();

  // Use the same HTML the editor holds (mapper round-trip), then document coords.
  QTextDocument doc;
  doc.setHtml(editor.html());
  int doc_pos = -1;
  for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
    if (b.text().contains(QStringLiteral("gamma"))) {
      doc_pos = b.position() + 1;  // inside the line, not only block start
      break;
    }
  }
  REQUIRE(doc_pos >= 0);

  REQUIRE(editor.toggleChecklistAtDocumentPosition(doc_pos));
  pump(80);

  auto loaded = store.load(seed.id);
  REQUIRE(loaded);
  bool found_done = false;
  for (const auto& bl : loaded.value().content.blocks()) {
    if (const auto* check = std::get_if<notes::domain::ChecklistBlock>(&bl)) {
      for (const auto& it : check->items()) {
        if (it.text == "gamma" && it.done) found_done = true;
      }
    }
  }
  REQUIRE(found_done);
}
