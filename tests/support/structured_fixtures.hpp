#pragma once

#include "domain/attachments/attachment_id.hpp"
#include "domain/notes/checklist.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_content.hpp"
#include "domain/notes/note_id.hpp"
#include "domain/folders/folder_id.hpp"

#include <string>
#include <utility>
#include <vector>

namespace notes::testing {

inline domain::NoteContent make_mixed_structured_content(
    const std::string& attachment_id = "att-fixture-1",
    const std::string& attachment_name = "photo.png") {
  domain::ParagraphBlock lead;
  lead.spans.push_back(domain::TextSpan{"Intro ", true, false, false});
  lead.spans.push_back(domain::TextSpan{"plain", false, false, false});

  domain::ChecklistBlock check{std::vector<domain::ChecklistItem>{
      {false, "buy milk"},
      {true, "write tests"},
  }};

  domain::ParagraphBlock mid;
  mid.spans.push_back(domain::TextSpan{"Between blocks", false, true, false});

  domain::AttachmentRefBlock att;
  att.attachment_id = domain::AttachmentId{attachment_id};
  att.display_name = attachment_name;

  domain::ParagraphBlock trail;
  trail.spans.push_back(domain::TextSpan{"Tail", false, false, true});

  return domain::NoteContent{std::vector<domain::ContentBlock>{
      std::move(lead), std::move(check), std::move(mid), std::move(att),
      std::move(trail)}};
}

inline domain::Note make_structured_note(
    const std::string& id = "note-struct-1",
    const std::string& folder = "root") {
  domain::Note n;
  n.id = domain::NoteId{id};
  n.folder_id = domain::FolderId{folder};
  n.title = "Structured";
  n.content = make_mixed_structured_content();
  n.created_at_ms = 1000;
  n.modified_at_ms = 1000;
  n.revision = 0;
  n.pinned = false;
  n.trashed_at_ms = 0;
  return n;
}

// True if content still has checklist + attachment identities (no silent flatten).
inline bool content_has_checklist_and_attachment(const domain::NoteContent& c) {
  bool has_check = false;
  bool has_att = false;
  for (const auto& b : c.blocks()) {
    if (std::holds_alternative<domain::ChecklistBlock>(b)) has_check = true;
    if (std::holds_alternative<domain::AttachmentRefBlock>(b)) has_att = true;
  }
  return has_check && has_att;
}

inline bool checklist_item_done_at(const domain::NoteContent& c, std::size_t item_index,
                                   bool expect_done) {
  for (const auto& b : c.blocks()) {
    if (const auto* ch = std::get_if<domain::ChecklistBlock>(&b)) {
      if (item_index < ch->items().size()) {
        return ch->items()[item_index].done == expect_done;
      }
    }
  }
  return false;
}

}  // namespace notes::testing
