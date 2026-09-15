#pragma once
#include "checklist.hpp"
#include "domain/attachments/attachment_id.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace notes::domain {

enum class InlineStyle { None, Bold, Italic, Underline };

struct TextSpan {
  std::string text;
  bool bold{false};
  bool italic{false};
  bool underline{false};
};

struct ParagraphBlock {
  std::vector<TextSpan> spans;
};

struct AttachmentRefBlock {
  AttachmentId attachment_id;
  std::string display_name;
};

using ContentBlock = std::variant<ParagraphBlock, ChecklistBlock, AttachmentRefBlock>;

class NoteContent {
public:
  NoteContent() = default;
  explicit NoteContent(std::vector<ContentBlock> blocks) : blocks_(std::move(blocks)) {}

  static NoteContent from_plain_text(std::string text) {
    ParagraphBlock p;
    p.spans.push_back(TextSpan{std::move(text), false, false, false});
    return NoteContent{std::vector<ContentBlock>{std::move(p)}};
  }

  [[nodiscard]] const std::vector<ContentBlock>& blocks() const noexcept { return blocks_; }

  [[nodiscard]] std::string plain_text() const {
    std::string out;
    for (const auto& block : blocks_) {
      if (!out.empty()) out.push_back('\n');
      if (const auto* p = std::get_if<ParagraphBlock>(&block)) {
        for (const auto& s : p->spans) out += s.text;
      } else if (const auto* c = std::get_if<ChecklistBlock>(&block)) {
        for (const auto& item : c->items()) {
          out += item.done ? "[x] " : "[ ] ";
          out += item.text;
          out.push_back('\n');
        }
        if (!out.empty() && out.back() == '\n') out.pop_back();
      } else if (const auto* a = std::get_if<AttachmentRefBlock>(&block)) {
        out += "[attachment:";
        out += a->display_name.empty() ? a->attachment_id.value() : a->display_name;
        out += "]";
      }
    }
    return out;
  }

  [[nodiscard]] bool empty() const noexcept { return blocks_.empty(); }

private:
  std::vector<ContentBlock> blocks_;
};

}  // namespace notes::domain
