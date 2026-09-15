#include "domain/notes/checklist.hpp"
#include "domain/notes/note_content.hpp"

#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
  if (!c) throw std::runtime_error(m);
}

int main() {
  using namespace notes::domain;
  try {
    auto c = NoteContent::from_plain_text("Title line\nBody");
    require(c.plain_text().find("Title line") != std::string::npos, "title");
    require(c.plain_text().find("Body") != std::string::npos, "body");

    std::vector<ChecklistItem> items;
    items.push_back(ChecklistItem{false, "one"});
    items.push_back(ChecklistItem{true, "two"});
    ChecklistBlock block{std::move(items)};
    auto toggled = block.with_toggled(0);
    require(toggled.items()[0].done, "checked");

    std::vector<ContentBlock> blocks;
    ParagraphBlock p;
    p.spans.push_back(TextSpan{"hi", true, false, false});
    blocks.emplace_back(std::move(p));
    blocks.emplace_back(toggled);
    NoteContent c2{std::move(blocks)};
    require(!c2.empty(), "not empty");
    require(c2.plain_text().find("[x] one") != std::string::npos, "plain");
    std::cerr << "domain note_content PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "FAIL " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
