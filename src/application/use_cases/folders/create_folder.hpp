#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/folders/folder_writer.hpp"
#include "application/result.hpp"
#include "domain/folders/folder.hpp"

#include <optional>
#include <string>
#include <utility>

namespace notes::application {

class CreateFolder {
public:
  CreateFolder(FolderWriter& writer, Clock& clock) : writer_(writer), clock_(clock) {}

  struct Request {
    std::string name;
    std::optional<domain::FolderId> parent_id;
    std::int64_t sort_order{0};
  };

  [[nodiscard]] Result<domain::Folder> execute(Request req) {
    if (req.name.empty()) {
      return Result<domain::Folder>::fail({ErrorKind::ValidationFailed, "name required"});
    }
    const auto now = clock_.now_ms();
    domain::Folder folder;
    folder.id = domain::FolderId{"folder-" + std::to_string(now) + "-" + std::to_string(++seq_)};
    folder.name = std::move(req.name);
    folder.parent_id = std::move(req.parent_id);
    folder.sort_order = req.sort_order;
    folder.created_at_ms = now;
    folder.modified_at_ms = now;
    return writer_.save(folder);
  }

private:
  static inline std::uint64_t seq_{0};
  FolderWriter& writer_;
  Clock& clock_;
};

}  // namespace notes::application
