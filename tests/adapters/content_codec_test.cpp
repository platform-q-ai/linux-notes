#include "adapters/persistence/sqlite/content_codec.hpp"
#include "domain/notes/note_content.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}

void expect_roundtrip(const notes::domain::NoteContent& content,
                      const char* label) {
  using notes::adapters::persistence::decode_content;
  using notes::adapters::persistence::encode_content;
  const auto encoded = encode_content(content);
  const auto decoded = decode_content(encoded);
  require(decoded.plain_text() == content.plain_text(), label);
  require(decoded.blocks().size() == content.blocks().size(),
          "block count mismatch");
}

}  // namespace

int main() {
  using notes::domain::AttachmentId;
  using notes::domain::AttachmentRefBlock;
  using notes::domain::ChecklistBlock;
  using notes::domain::ChecklistItem;
  using notes::domain::ContentBlock;
  using notes::domain::NoteContent;
  using notes::domain::ParagraphBlock;
  using notes::domain::TextSpan;
  using notes::adapters::persistence::decode_content;
  using notes::adapters::persistence::encode_content;

  try {
    // P1: multiline plain text must survive encode/decode (was empty).
    {
      const auto c = NoteContent::from_plain_text("line1\nline2");
      const auto enc = encode_content(c);
      const auto dec = decode_content(enc);
      require(dec.plain_text() == "line1\nline2",
              "multiline plain roundtrip body");
      require(!dec.plain_text().empty(), "multiline not empty");
    }

    // Unicode + escaping-ish content.
    {
      const auto c = NoteContent::from_plain_text(
          "hello 世界\nsecond café\nthird 🎵");
      expect_roundtrip(c, "unicode multiline plain");
    }

    // Embedded newline inside a bold span.
    {
      ParagraphBlock p;
      p.spans.push_back(TextSpan{"bold\nline", true, false, false});
      p.spans.push_back(TextSpan{" plain", false, true, false});
      NoteContent c{std::vector<ContentBlock>{std::move(p)}};
      const auto dec = decode_content(encode_content(c));
      require(dec.blocks().size() == 1, "one para");
      const auto* bp = std::get_if<ParagraphBlock>(&dec.blocks()[0]);
      require(bp != nullptr && bp->spans.size() == 2, "two spans");
      require(bp->spans[0].text == "bold\nline", "bold span text");
      require(bp->spans[0].bold && !bp->spans[0].italic, "bold flags");
      require(bp->spans[1].text == " plain" && bp->spans[1].italic,
              "italic span");
    }

    // Multi-block paragraphs (no embedded newline) still work.
    {
      ParagraphBlock a;
      a.spans.push_back(TextSpan{"line1", false, false, false});
      ParagraphBlock b;
      b.spans.push_back(TextSpan{"line2", false, false, false});
      NoteContent c{std::vector<ContentBlock>{std::move(a), std::move(b)}};
      expect_roundtrip(c, "multi-block paragraphs");
    }

    // Checklist item text with embedded newline.
    {
      ChecklistBlock check{std::vector<ChecklistItem>{
          ChecklistItem{false, "one"},
          ChecklistItem{true, "two\nthree"},
      }};
      NoteContent c{std::vector<ContentBlock>{std::move(check)}};
      const auto dec = decode_content(encode_content(c));
      const auto* cb = std::get_if<ChecklistBlock>(&dec.blocks().at(0));
      require(cb != nullptr && cb->items().size() == 2, "check items");
      require(cb->items()[0].text == "one" && !cb->items()[0].done, "item0");
      require(cb->items()[1].text == "two\nthree" && cb->items()[1].done,
              "item1 newline");
    }

    // Attachment display name with newline.
    {
      AttachmentRefBlock att{AttachmentId{std::string{"att-1"}},
                             std::string{"file\nname.txt"}};
      NoteContent c{std::vector<ContentBlock>{std::move(att)}};
      const auto dec = decode_content(encode_content(c));
      const auto* ab = std::get_if<AttachmentRefBlock>(&dec.blocks().at(0));
      require(ab != nullptr, "att block");
      require(ab->attachment_id.value() == "att-1", "att id");
      require(ab->display_name == "file\nname.txt", "att name newline");
    }

    // Empty span / empty body.
    {
      expect_roundtrip(NoteContent::from_plain_text(""), "empty plain");
    }

    // Legacy non-V1 blob still treated as plain text.
    {
      const auto dec = decode_content("raw legacy body\nwith lines");
      require(dec.plain_text() == "raw legacy body\nwith lines", "legacy");
    }

    std::cerr << "content_codec_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "content_codec_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
