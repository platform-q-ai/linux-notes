#pragma once

#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_searcher.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "domain/folders/folder_id.hpp"
#include "domain/notes/note.hpp"
#include "domain/notes/note_content.hpp"
#include "domain/notes/note_id.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace notes::tests {

inline void require(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}

// Shared contract for NoteReader/Writer/Searcher (CAS: note.revision is base).
template <typename Store>
void run_note_store_contract(Store& store, const char* label) {
  using namespace notes::domain;
  using namespace notes::application;
  std::cerr << "[contract] " << label << " begin\n";

  const FolderId folder{std::string{"root"}};
  const NoteId id{std::string{"note-contract-1"}};

  {
    auto r = store.load(id);
    require(!r.has_value() && r.error().kind == ErrorKind::NotFound,
            "missing load");
  }

  Note n1;
  n1.id = id;
  n1.folder_id = folder;
  n1.title = "Hello";
  n1.content = NoteContent::from_plain_text("hello world");
  n1.created_at_ms = 1000;
  n1.modified_at_ms = 1000;
  n1.revision = 0;
  n1.pinned = false;
  {
    auto s = store.save(n1);
    require(s.has_value(), "save insert");
    require(s.value().revision == 1, "rev1 assigned");
  }
  {
    auto r = store.load(id);
    require(r.has_value(), "load after insert");
    require(r.value().revision == 1, "rev1");
    require(r.value().content.plain_text().find("hello") != std::string::npos,
            "body");
  }

  Note n2 = n1;
  n2.content = NoteContent::from_plain_text("hello updated");
  n2.revision = 1;  // base
  n2.modified_at_ms = 2000;
  {
    auto s = store.save(n2);
    require(s.has_value(), "cas ok");
    require(s.value().revision == 2, "rev2");
  }

  Note stale = n2;
  stale.content = NoteContent::from_plain_text("stale");
  stale.revision = 1;  // wrong base
  {
    auto s = store.save(stale);
    require(!s.has_value() && s.error().kind == ErrorKind::RevisionConflict,
            "cas conflict");
  }
  {
    auto r = store.load(id);
    require(r.has_value() && r.value().revision == 2, "unchanged");
    require(r.value().content.plain_text().find("updated") != std::string::npos,
            "body unchanged");
  }

  {
    auto lst = store.list(folder);
    require(lst.has_value() && !lst.value().empty(), "list");
    bool found = false;
    for (const auto& s : lst.value()) {
      if (s.id == id) found = true;
    }
    require(found, "list contains");
  }

  {
    auto sr = store.search("updated");
    require(sr.has_value(), "search ok");
    bool found = false;
    for (const auto& s : sr.value()) {
      if (s.id == id) found = true;
    }
    require(found, "search hit");
  }

  {
    auto rm = store.remove(id);
    require(rm.has_value(), "remove");
    auto r = store.load(id);
    require(!r.has_value() && r.error().kind == ErrorKind::NotFound, "gone");
  }

  std::cerr << "[contract] " << label << " PASS\n";
}

}  // namespace notes::tests
