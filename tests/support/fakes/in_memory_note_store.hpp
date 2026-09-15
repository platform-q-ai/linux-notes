#pragma once
#include "application/ports/folders/folder_reader.hpp"
#include "application/ports/folders/folder_writer.hpp"
#include "application/ports/notes/note_reader.hpp"
#include "application/ports/notes/note_searcher.hpp"
#include "application/ports/notes/note_writer.hpp"
#include "domain/notes/note.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace notes::testing {

class InMemoryNoteStore final : public application::NoteReader,
                                public application::NoteWriter,
                                public application::NoteSearcher,
                                public application::FolderReader,
                                public application::FolderWriter {
public:
  [[nodiscard]] application::Result<domain::Note> load(const domain::NoteId& id) const override {
    std::lock_guard lock(mu_);
    auto it = notes_.find(id);
    if (it == notes_.end()) {
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::NotFound, "note not found"});
    }
    return application::Result<domain::Note>::ok(it->second);
  }

  [[nodiscard]] application::Result<std::vector<domain::NoteSummary>> list(
      const domain::FolderId& folder_id) const override {
    std::lock_guard lock(mu_);
    std::vector<domain::NoteSummary> out;
    for (const auto& [id, note] : notes_) {
      if (note.folder_id == folder_id) out.push_back(to_summary(note));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
      if (a.pinned != b.pinned) return a.pinned > b.pinned;
      return a.modified_at_ms > b.modified_at_ms;
    });
    return application::Result<std::vector<domain::NoteSummary>>::ok(std::move(out));
  }

  [[nodiscard]] application::Result<domain::Note> save(const domain::Note& note) override {
    std::lock_guard lock(mu_);
    auto it = notes_.find(note.id);
    if (it == notes_.end()) {
      if (note.revision != 0) {
        return application::Result<domain::Note>::fail(
            {application::ErrorKind::RevisionConflict, "missing base"});
      }
      domain::Note stored = note;
      stored.revision = 1;
      notes_[stored.id] = stored;
      return application::Result<domain::Note>::ok(stored);
    }
    if (it->second.revision != note.revision) {
      return application::Result<domain::Note>::fail(
          {application::ErrorKind::RevisionConflict, "CAS failed"});
    }
    domain::Note stored = note;
    stored.revision = note.revision + 1;
    it->second = stored;
    return application::Result<domain::Note>::ok(stored);
  }

  [[nodiscard]] application::Result<void> remove(const domain::NoteId& id) override {
    std::lock_guard lock(mu_);
    if (notes_.erase(id) == 0) {
      return application::Result<void>::fail({application::ErrorKind::NotFound, "note not found"});
    }
    return application::Result<void>::ok();
  }

  [[nodiscard]] application::Result<std::vector<domain::NoteSummary>> search(
      const std::string& query) const override {
    std::lock_guard lock(mu_);
    const auto q = to_lower(query);
    std::vector<domain::NoteSummary> out;
    for (const auto& [id, note] : notes_) {
      const auto hay = to_lower(note.title + " " + note.content.plain_text());
      if (q.empty() || hay.find(q) != std::string::npos) out.push_back(to_summary(note));
    }
    return application::Result<std::vector<domain::NoteSummary>>::ok(std::move(out));
  }

  [[nodiscard]] application::Result<domain::Folder> load(
      const domain::FolderId& id) const override {
    std::lock_guard lock(mu_);
    auto it = folders_.find(id);
    if (it == folders_.end()) {
      return application::Result<domain::Folder>::fail(
          {application::ErrorKind::NotFound, "folder not found"});
    }
    return application::Result<domain::Folder>::ok(it->second);
  }

  [[nodiscard]] application::Result<std::vector<domain::Folder>> list_all() const override {
    std::lock_guard lock(mu_);
    std::vector<domain::Folder> out;
    out.reserve(folders_.size());
    for (const auto& [id, f] : folders_) out.push_back(f);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
      if (a.sort_order != b.sort_order) return a.sort_order < b.sort_order;
      return a.name < b.name;
    });
    return application::Result<std::vector<domain::Folder>>::ok(std::move(out));
  }

  [[nodiscard]] application::Result<domain::Folder> save(const domain::Folder& folder) override {
    std::lock_guard lock(mu_);
    folders_[folder.id] = folder;
    return application::Result<domain::Folder>::ok(folder);
  }

  [[nodiscard]] application::Result<void> remove(const domain::FolderId& id) override {
    std::lock_guard lock(mu_);
    if (folders_.erase(id) == 0) {
      return application::Result<void>::fail(
          {application::ErrorKind::NotFound, "folder not found"});
    }
    return application::Result<void>::ok();
  }

private:
  static domain::NoteSummary to_summary(const domain::Note& note) {
    domain::NoteSummary s;
    s.id = note.id;
    s.folder_id = note.folder_id;
    s.title = note.title;
    s.preview = note.content.plain_text();
    if (s.preview.size() > 120) s.preview.resize(120);
    s.modified_at_ms = note.modified_at_ms;
    s.revision = note.revision;
    s.pinned = note.pinned;
    return s;
  }

  static std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  mutable std::mutex mu_;
  std::map<domain::NoteId, domain::Note> notes_;
  std::map<domain::FolderId, domain::Folder> folders_;
};

}  // namespace notes::testing
