#pragma once

#include "application/ports/notes/note_writer.hpp"

#include <atomic>

namespace notes::testing {

// Inject storage failures around an inner NoteWriter (presentation/lifecycle tests).
class ControllableNoteWriter final : public application::NoteWriter {
public:
  explicit ControllableNoteWriter(application::NoteWriter& inner) : inner_(inner) {}

  std::atomic<bool> fail_next_save{false};
  std::atomic<bool> fail_next_trash{false};
  std::atomic<bool> fail_next_restore{false};
  std::atomic<bool> fail_next_remove{false};
  std::atomic<int> save_calls{0};
  std::atomic<int> trash_calls{0};
  std::atomic<int> restore_calls{0};
  std::atomic<int> remove_calls{0};

  [[nodiscard]] application::Result<domain::Note> save(
      const domain::Note& note) override {
    ++save_calls;
    if (fail_next_save.exchange(false)) {
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::StorageFailure, "injected save failure"});
    }
    return inner_.save(note);
  }

  [[nodiscard]] application::Result<void> trash(
      const domain::NoteId& id, std::int64_t trashed_at_ms) override {
    ++trash_calls;
    if (fail_next_trash.exchange(false)) {
      return application::Result<void>::fail(
          {application::ErrorKind::StorageFailure, "injected trash failure"});
    }
    return inner_.trash(id, trashed_at_ms);
  }

  [[nodiscard]] application::Result<domain::Note> restore(
      const domain::NoteId& id,
      const domain::FolderId& restore_folder_id) override {
    ++restore_calls;
    if (fail_next_restore.exchange(false)) {
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::StorageFailure, "injected restore failure"});
    }
    return inner_.restore(id, restore_folder_id);
  }

  [[nodiscard]] application::Result<void> remove(
      const domain::NoteId& id) override {
    ++remove_calls;
    if (fail_next_remove.exchange(false)) {
      return application::Result<void>::fail(
          {application::ErrorKind::StorageFailure, "injected remove failure"});
    }
    return inner_.remove(id);
  }

private:
  application::NoteWriter& inner_;
};

}  // namespace notes::testing
