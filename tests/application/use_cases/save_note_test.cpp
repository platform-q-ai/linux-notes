#include "application/use_cases/notes/create_note.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
  if (!c) throw std::runtime_error(m);
}

int main() {
  using namespace notes;
  try {
    testing::InMemoryNoteStore store;
    testing::FixedClock clock{1'000'000};

    application::CreateNote create(store, clock);
    application::CreateNote::Request creq;
    creq.folder_id = domain::FolderId{std::string{"root"}};
    creq.title = "A";
    creq.content = domain::NoteContent::from_plain_text("A body");
    auto created = create.execute(std::move(creq));
    require(created.has_value(), "create");
    auto note = created.value();
    require(note.revision == 1, "rev1");

    // Concurrent edit bumps revision
    domain::Note concurrent = note;
    concurrent.content = domain::NoteContent::from_plain_text("B");
    concurrent.revision = 1;
    auto cs = store.save(concurrent);
    require(cs.has_value() && cs.value().revision == 2, "concurrent");

    application::SaveNote save(store, store, clock);
    application::SaveNote::Request req;
    req.note = note;  // still base revision 1 (stale)
    req.note.content = domain::NoteContent::from_plain_text("C from stale");
    req.note.revision = 1;
    req.allow_keep_both = true;
    auto r = save.execute(std::move(req));
    require(r.has_value(), "keep both");
    require(r.value().kept_both, "flag");
    require(!r.value().preserved_prior_id.empty(), "preserved id");

    // prior remains under original id at rev 2 with body B
    auto prior = store.load(note.id);
    require(prior.has_value(), "prior");
    require(prior.value().content.plain_text().find('B') != std::string::npos,
            "prior body");
    auto conflict = store.load(r.value().saved.id);
    require(conflict.has_value(), "conflict note");
    require(conflict.value().content.plain_text().find('C') !=
                std::string::npos,
            "conflict body");

    std::cerr << "save_note keep-both PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "FAIL " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
