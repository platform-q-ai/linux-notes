#include <catch_amalgamated.hpp>
#include "domain/notes/note_content.hpp"

TEST_CASE("plain text content roundtrip helpers") {
  auto content = notes::domain::NoteContent::from_plain_text("Hello");
  REQUIRE(content.plain_text() == "Hello");
  REQUIRE_FALSE(content.blocks().empty());
}

TEST_CASE("checklist toggle returns new value") {
  notes::domain::ChecklistBlock block{
      {{false, "a"}, {true, "b"}}};
  auto toggled = block.with_toggled(0);
  REQUIRE(toggled.items().at(0).done);
  REQUIRE_FALSE(block.items().at(0).done);
}
