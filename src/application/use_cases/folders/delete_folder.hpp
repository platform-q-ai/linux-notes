#pragma once
#include "application/ports/folders/folder_writer.hpp"
#include "application/result.hpp"
#include "domain/folders/folder_id.hpp"

namespace notes::application {

class DeleteFolder {
public:
  explicit DeleteFolder(FolderWriter& writer) : writer_(writer) {}

  [[nodiscard]] Result<void> execute(const domain::FolderId& id) {
    if (id.empty()) {
      return Result<void>::fail({ErrorKind::ValidationFailed, "folder id required"});
    }
    return writer_.remove(id);
  }

private:
  FolderWriter& writer_;
};

}  // namespace notes::application
