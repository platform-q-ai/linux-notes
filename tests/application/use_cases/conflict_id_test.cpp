#include "application/ports/id_source.hpp"
#include "application/use_cases/notes/save_note.hpp"
#include "tests/support/fakes/fixed_clock.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

void require(bool c, const char* m) {
  if (!c) throw std::runtime_error(m);
}

// Deterministic IdSource that models a single process boot sequence.
class SeqIdSource final : public notes::application::IdSource {
public:
  explicit SeqIdSource(std::string process_tag, std::uint64_t start = 0)
      : tag_(std::move(process_tag)), n_(start) {}

  std::string next_unique_token() override {
    return tag_ + "-" + std::to_string(++n_);
  }

  // Simulate process restart: sequence resets, tag stays (or changes).
  void restart() { n_ = 0; }

private:
  std::string tag_;
  std::uint64_t n_{0};
};

notes::domain::Note make_base(const std::string& id) {
  notes::domain::Note base;
  base.id = notes::domain::NoteId{std::string{id}};
  base.folder_id = notes::domain::FolderId{std::string{"root"}};
  base.title = "shared";
  base.content = notes::domain::NoteContent::from_plain_text("v0");
  base.revision = 0;
  base.created_at_ms = 1;
  base.modified_at_ms = 1;
  return base;
}

std::string keep_both_once(notes::testing::InMemoryNoteStore& store,
                           notes::testing::FixedClock& clock,
                           notes::application::IdSource* ids,
                           notes::domain::Note stale_base) {
  // Ensure head is ahead of stale base revision.
  auto head = store.load(stale_base.id).value();
  head.content = notes::domain::NoteContent::from_plain_text("head");
  // revision already bumped by prior saves; force conflict vs stale_base.revision
  notes::application::SaveNote save(store, store, clock, ids);
  notes::application::SaveNote::Request kb;
  kb.note = std::move(stale_base);
  kb.note.content = notes::domain::NoteContent::from_plain_text("conflict-body");
  kb.allow_keep_both = true;
  auto out = save.execute(std::move(kb));
  require(out.has_value() && out.value().kept_both, "keep both");
  return out.value().saved.id.value();
}

void test_in_process_unique_under_fixed_clock() {
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{42};
  SeqIdSource ids("p1");

  auto base = make_base("shared-a");
  require(static_cast<bool>(store.save(base)), "insert");
  auto head = store.load(base.id).value();
  head.content = notes::domain::NoteContent::from_plain_text("head");
  head.revision = 1;
  require(static_cast<bool>(store.save(head)), "head");

  std::unordered_set<std::string> seen;
  for (int i = 0; i < 5; ++i) {
    notes::domain::Note stale = base;
    stale.revision = 1;
    stale.content = notes::domain::NoteContent::from_plain_text("c" + std::to_string(i));
    notes::application::SaveNote save(store, store, clock, &ids);
    notes::application::SaveNote::Request kb;
    kb.note = stale;
    kb.allow_keep_both = true;
    auto out = save.execute(std::move(kb));
    require(out.has_value() && out.value().kept_both, "kb");
    const auto& id = out.value().saved.id.value();
    require(id.find("note-conflict-42-") == 0, "prefix+ms");
    require(seen.insert(id).second, "unique in process");
  }
  require(seen.size() == 5, "five ids");
}

void test_restart_same_clock_no_collision() {
  // Models: process A produces conflict ids, exits (seq would reset), process B
  // boots with same clock reading and a reset counter but distinct identity tag
  // (or entropy). Old process-local ++seq would collide on note-conflict-42-1.
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{42};

  auto base = make_base("shared-b");
  require(static_cast<bool>(store.save(base)), "insert");
  auto head = store.load(base.id).value();
  head.content = notes::domain::NoteContent::from_plain_text("head");
  head.revision = 1;
  require(static_cast<bool>(store.save(head)), "head");

  SeqIdSource proc_a("boot-A");
  notes::domain::Note stale = base;
  stale.revision = 1;
  const std::string id_a = [&] {
    notes::application::SaveNote save(store, store, clock, &proc_a);
    notes::application::SaveNote::Request kb;
    kb.note = stale;
    kb.note.content = notes::domain::NoteContent::from_plain_text("from-a");
    kb.allow_keep_both = true;
    auto out = save.execute(std::move(kb));
    require(out.has_value(), "a ok");
    return out.value().saved.id.value();
  }();

  // Restart: sequence resets to 0 — old design would emit the same ...-1 suffix.
  proc_a.restart();
  // After restart the same SeqIdSource tag+reset would collide; a new boot gets
  // a new process tag (or random token). Model independent process B:
  SeqIdSource proc_b("boot-B");
  const std::string id_b = [&] {
    notes::domain::Note stale2 = base;
    stale2.revision = 1;
    stale2.content = notes::domain::NoteContent::from_plain_text("from-b");
    notes::application::SaveNote save(store, store, clock, &proc_b);
    notes::application::SaveNote::Request kb;
    kb.note = stale2;
    kb.allow_keep_both = true;
    auto out = save.execute(std::move(kb));
    require(out.has_value(), "b ok");
    return out.value().saved.id.value();
  }();

  require(id_a != id_b, "restart/multi-instance ids differ at same clock");
  require(id_a.find("note-conflict-42-") == 0, "a form");
  require(id_b.find("note-conflict-42-") == 0, "b form");
  require(id_a.find("boot-A") != std::string::npos, "a identity");
  require(id_b.find("boot-B") != std::string::npos, "b identity");
}

void test_two_independent_sources_same_clock() {
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{99};

  auto base = make_base("shared-c");
  require(static_cast<bool>(store.save(base)), "insert");
  auto head = store.load(base.id).value();
  head.revision = 1;
  head.content = notes::domain::NoteContent::from_plain_text("head");
  require(static_cast<bool>(store.save(head)), "head");

  SeqIdSource left("L");
  SeqIdSource right("R");
  std::unordered_set<std::string> ids;
  for (int i = 0; i < 3; ++i) {
    for (notes::application::IdSource* src : {static_cast<notes::application::IdSource*>(&left),
                                              static_cast<notes::application::IdSource*>(&right)}) {
      notes::domain::Note stale = base;
      stale.revision = 1;
      stale.content = notes::domain::NoteContent::from_plain_text("x");
      notes::application::SaveNote save(store, store, clock, src);
      notes::application::SaveNote::Request kb;
      kb.note = stale;
      kb.allow_keep_both = true;
      auto out = save.execute(std::move(kb));
      require(out.has_value() && out.value().kept_both, "kb multi");
      require(ids.insert(out.value().saved.id.value()).second, "no collision");
    }
  }
  require(ids.size() == 6, "six distinct");
}

void test_default_entropy_source_no_injection() {
  // Without injected IdSource, default path must still avoid pure seq collision
  // across independent SaveNote instances at fixed clock (simulates multi-instance).
  notes::testing::InMemoryNoteStore store;
  notes::testing::FixedClock clock{7};

  auto base = make_base("shared-d");
  require(static_cast<bool>(store.save(base)), "insert");
  auto head = store.load(base.id).value();
  head.revision = 1;
  head.content = notes::domain::NoteContent::from_plain_text("head");
  require(static_cast<bool>(store.save(head)), "head");

  std::unordered_set<std::string> ids;
  for (int i = 0; i < 20; ++i) {
    notes::domain::Note stale = base;
    stale.revision = 1;
    stale.content = notes::domain::NoteContent::from_plain_text("c");
    // Fresh SaveNote object each time — no shared static seq identity.
    notes::application::SaveNote save(store, store, clock, nullptr);
    notes::application::SaveNote::Request kb;
    kb.note = stale;
    kb.allow_keep_both = true;
    auto out = save.execute(std::move(kb));
    require(out.has_value() && out.value().kept_both, "default kb");
    const auto& id = out.value().saved.id.value();
    require(id.find("note-conflict-7-") == 0, "form");
    // Token after ms must not be a tiny integer-only seq like "1","2" only —
    // require substantial entropy suffix (hex length >= 16).
    const auto pos = id.find_last_of('-');
    require(pos != std::string::npos, "dash");
    const auto token = id.substr(pos + 1);
    require(token.size() >= 16, "entropy token length");
    require(ids.insert(id).second, "default unique");
  }
}

}  // namespace

int main() {
  try {
    test_in_process_unique_under_fixed_clock();
    test_restart_same_clock_no_collision();
    test_two_independent_sources_same_clock();
    test_default_entropy_source_no_injection();
    std::cerr << "conflict_id_test PASS\n";
  } catch (const std::exception& ex) {
    std::cerr << "conflict_id_test FAIL: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
