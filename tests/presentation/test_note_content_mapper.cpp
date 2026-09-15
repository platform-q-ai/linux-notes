#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

#include "domain/attachments/attachment_id.hpp"
#include "domain/notes/checklist.hpp"
#include "domain/notes/note_content.hpp"
#include "presentation/qt/mapping/note_content_document_mapper.hpp"

#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <string>
#include <variant>
#include <vector>

using notes::domain::AttachmentId;
using notes::domain::AttachmentRefBlock;
using notes::domain::ChecklistBlock;
using notes::domain::ChecklistItem;
using notes::domain::ContentBlock;
using notes::domain::NoteContent;
using notes::domain::ParagraphBlock;
using notes::domain::TextSpan;
using notes::presentation::NoteContentDocumentMapper;

namespace {

NoteContent make_mixed_note() {
  ParagraphBlock intro;
  intro.spans.push_back(TextSpan{"Intro ", true, false, false});
  intro.spans.push_back(TextSpan{"line", false, true, false});

  ChecklistBlock checks{std::vector<ChecklistItem>{
      ChecklistItem{false, "buy milk"},
      ChecklistItem{true, "write tests"},
  }};

  ParagraphBlock mid;
  mid.spans.push_back(TextSpan{"Middle plain", false, false, true});

  AttachmentRefBlock att{AttachmentId{"att-42"}, "photo.png"};

  ParagraphBlock tail;
  tail.spans.push_back(TextSpan{"Tail", false, false, false});

  std::vector<ContentBlock> blocks;
  blocks.emplace_back(std::move(intro));
  blocks.emplace_back(std::move(checks));
  blocks.emplace_back(std::move(mid));
  blocks.emplace_back(std::move(att));
  blocks.emplace_back(std::move(tail));
  return NoteContent{std::move(blocks)};
}

void require_mixed_structure(const NoteContent& c, const char* where) {
  INFO(where);
  REQUIRE(c.blocks().size() == 5);

  const auto* intro = std::get_if<ParagraphBlock>(&c.blocks()[0]);
  REQUIRE(intro != nullptr);
  REQUIRE(intro->spans.size() >= 1);
  REQUIRE(intro->spans[0].bold);
  bool saw_italic = false;
  for (const auto& s : intro->spans) {
    if (s.italic) saw_italic = true;
  }
  REQUIRE(saw_italic);

  const auto* checks = std::get_if<ChecklistBlock>(&c.blocks()[1]);
  REQUIRE(checks != nullptr);
  REQUIRE(checks->items().size() == 2);
  REQUIRE(checks->items()[0].done == false);
  REQUIRE(checks->items()[0].text == "buy milk");
  REQUIRE(checks->items()[1].done == true);
  REQUIRE(checks->items()[1].text == "write tests");

  const auto* mid = std::get_if<ParagraphBlock>(&c.blocks()[2]);
  REQUIRE(mid != nullptr);
  std::string mid_text;
  for (const auto& s : mid->spans) mid_text += s.text;
  REQUIRE(mid_text.find("Middle") != std::string::npos);

  const auto* att = std::get_if<AttachmentRefBlock>(&c.blocks()[3]);
  REQUIRE(att != nullptr);
  REQUIRE(att->attachment_id.value() == "att-42");
  REQUIRE(att->display_name == "photo.png");

  const auto* tail = std::get_if<ParagraphBlock>(&c.blocks()[4]);
  REQUIRE(tail != nullptr);
}

}  // namespace

TEST_CASE("mapper applyToDocument/fromDocument preserves structured blocks",
          "[mapper][structured][roundtrip]") {
  const NoteContent original = make_mixed_note();
  QTextDocument doc;
  NoteContentDocumentMapper::applyToDocument(original, doc);
  const NoteContent back = NoteContentDocumentMapper::fromDocument(doc);
  require_mixed_structure(back, "fromDocument");
}

TEST_CASE("mapper HTML roundtrip preserves checklist and attachment identity",
          "[mapper][structured][html]") {
  const NoteContent original = make_mixed_note();
  const QString html = NoteContentDocumentMapper::toHtml(original);
  const NoteContent back = NoteContentDocumentMapper::fromHtml(html);
  require_mixed_structure(back, "fromHtml");
}

TEST_CASE("editing adjacent rich text does not flatten structured blocks",
          "[mapper][structured][edit-adjacent]") {
  const NoteContent original = make_mixed_note();
  QTextDocument doc;
  NoteContentDocumentMapper::applyToDocument(original, doc);

  // Edit the middle plain paragraph without selecting the block separator
  // (BlockUnderCursor includes U+2029 and merges adjacent blocks).
  for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
    if (b.text().contains(QStringLiteral("Middle"))) {
      QTextCursor cur(b);
      cur.movePosition(QTextCursor::StartOfBlock);
      cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
      QTextCharFormat fmt;
      fmt.setFontUnderline(true);
      fmt.setFontWeight(QFont::Bold);
      cur.insertText(QStringLiteral("Middle edited"), fmt);
      break;
    }
  }

  // Also tweak intro span text without touching checklist/attachment.
  {
    QTextCursor cur(&doc);
    cur.movePosition(QTextCursor::Start);
    cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    // leave styles; append a suffix via end-of-block insert
    cur.clearSelection();
    cur.movePosition(QTextCursor::EndOfBlock);
    cur.insertText(QStringLiteral("!"));
  }

  const NoteContent back = NoteContentDocumentMapper::fromDocument(doc);
  REQUIRE(back.blocks().size() == 5);

  const auto* checks = std::get_if<ChecklistBlock>(&back.blocks()[1]);
  REQUIRE(checks != nullptr);
  REQUIRE(checks->items().size() == 2);
  REQUIRE(checks->items()[0].text == "buy milk");
  REQUIRE(checks->items()[1].done);

  const auto* att = std::get_if<AttachmentRefBlock>(&back.blocks()[3]);
  REQUIRE(att != nullptr);
  REQUIRE(att->attachment_id.value() == "att-42");

  const auto* mid = std::get_if<ParagraphBlock>(&back.blocks()[2]);
  REQUIRE(mid != nullptr);
  std::string mid_text;
  for (const auto& s : mid->spans) mid_text += s.text;
  REQUIRE(mid_text.find("Middle edited") != std::string::npos);
  bool mid_bold = false;
  for (const auto& s : mid->spans) {
    if (s.bold) mid_bold = true;
  }
  REQUIRE(mid_bold);

  // HTML path after adjacent edit must still keep structure.
  const QString html = doc.toHtml();
  const NoteContent via_html = NoteContentDocumentMapper::fromHtml(html);
  const auto* checks2 = std::get_if<ChecklistBlock>(&via_html.blocks()[1]);
  REQUIRE(checks2 != nullptr);
  REQUIRE(checks2->items().size() == 2);
  const auto* att2 = std::get_if<AttachmentRefBlock>(&via_html.blocks()[3]);
  REQUIRE(att2 != nullptr);
  REQUIRE(att2->attachment_id.value() == "att-42");
}

TEST_CASE("B/I/U paragraph formatting still roundtrips with structured neighbors",
          "[mapper][formatting]") {
  ParagraphBlock p;
  p.spans.push_back(TextSpan{"Bold", true, false, false});
  p.spans.push_back(TextSpan{" Italic", false, true, false});
  p.spans.push_back(TextSpan{" Under", false, false, true});

  ChecklistBlock checks{std::vector<ChecklistItem>{ChecklistItem{false, "item"}}};
  AttachmentRefBlock att{AttachmentId{"att-1"}, "file.bin"};

  NoteContent content{std::vector<ContentBlock>{p, checks, att}};
  QTextDocument doc;
  NoteContentDocumentMapper::applyToDocument(content, doc);

  // Toggle bold on first word via mapper helper (selection path).
  {
    QTextCursor cur(&doc);
    cur.movePosition(QTextCursor::Start);
    cur.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
    NoteContentDocumentMapper::toggleBold(cur);
  }

  const NoteContent back =
      NoteContentDocumentMapper::fromHtml(doc.toHtml());
  REQUIRE(back.blocks().size() == 3);
  const auto* para = std::get_if<ParagraphBlock>(&back.blocks()[0]);
  REQUIRE(para != nullptr);
  REQUIRE(std::get_if<ChecklistBlock>(&back.blocks()[1]) != nullptr);
  REQUIRE(std::get_if<AttachmentRefBlock>(&back.blocks()[2]) != nullptr);
  REQUIRE(std::get_if<AttachmentRefBlock>(&back.blocks()[2])->attachment_id.value() ==
          "att-1");
}

TEST_CASE("legacy plain attachment marker is recovered",
          "[mapper][legacy]") {
  QTextDocument doc;
  QTextCursor cur(&doc);
  cur.insertText(QStringLiteral("before"));
  cur.insertBlock();
  cur.insertText(QStringLiteral("[attachment:att-legacy-9 my doc.pdf]"));
  cur.insertBlock();
  cur.insertText(QStringLiteral("[ ] todo"));
  cur.insertBlock();
  cur.insertText(QStringLiteral("[x] done"));

  const NoteContent back = NoteContentDocumentMapper::fromDocument(doc);
  REQUIRE(back.blocks().size() == 3);
  REQUIRE(std::get_if<ParagraphBlock>(&back.blocks()[0]) != nullptr);
  const auto* att = std::get_if<AttachmentRefBlock>(&back.blocks()[1]);
  REQUIRE(att != nullptr);
  REQUIRE(att->attachment_id.value() == "att-legacy-9");
  REQUIRE(att->display_name == "my doc.pdf");
  const auto* checks = std::get_if<ChecklistBlock>(&back.blocks()[2]);
  REQUIRE(checks != nullptr);
  REQUIRE(checks->items().size() == 2);
  REQUIRE_FALSE(checks->items()[0].done);
  REQUIRE(checks->items()[1].done);
}

TEST_CASE("forged attachment markers with traversal ids stay plain text",
          "[mapper][security]") {
  QTextDocument doc;
  QTextCursor cur(&doc);
  cur.insertText(QStringLiteral("[attachment:../outside/leak secret]"));
  cur.insertBlock();
  cur.insertText(QStringLiteral("[attachment:/tmp/x bin]"));
  cur.insertBlock();
  cur.insertText(QStringLiteral("[attachment:att-ok safe name]"));

  const NoteContent back = NoteContentDocumentMapper::fromDocument(doc);
  REQUIRE(back.blocks().size() == 3);
  REQUIRE(std::get_if<ParagraphBlock>(&back.blocks()[0]) != nullptr);
  REQUIRE(std::get_if<ParagraphBlock>(&back.blocks()[1]) != nullptr);
  const auto* att = std::get_if<AttachmentRefBlock>(&back.blocks()[2]);
  REQUIRE(att != nullptr);
  REQUIRE(att->attachment_id.value() == "att-ok");
  REQUIRE(att->display_name == "safe name");
}
