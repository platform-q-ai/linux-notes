#pragma once
#include "application/ports/clock.hpp"
#include "application/ports/folders/folder_reader.hpp"
#include "application/ports/folders/folder_writer.hpp"
#include "application/result.hpp"
#include "domain/folders/folder.hpp"

#include <string>
#include <utility>

namespace notes::application {

class RenameFolder {
public:
  RenameFolder(FolderReader& reader, FolderWriter& writer, Clock& clock)
      : reader_(reader), writer_(writer), clock_(clock) {}

  struct Request {
    domain::FolderId id;
    std::string name;
  };

  [[nodiscard]] Result<domain::Folder> execute(Request req) {
    if (req.name.empty()) {
      return Result<domain::Folder>::fail({ErrorKind::ValidationFailed, "name required"});
    }
    auto loaded = reader_.load(req.id);
    if (!loaded) return loaded;
    auto folder = std::move(loaded.value());
    folder.name = std::move(req.name);
    folder.modified_at_ms = clock_.now_ms();
    return writer_.save(folder);
  }

private:
  FolderReader& reader_;
  FolderWriter& writer_;
  Clock& clock_;
};

}  // namespace notes::application
