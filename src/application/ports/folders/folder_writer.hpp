#pragma once
#include "application/result.hpp"
#include "domain/folders/folder.hpp"
#include "domain/folders/folder_id.hpp"

namespace notes::application {

class FolderWriter {
public:
  virtual ~FolderWriter() = default;
  [[nodiscard]] virtual Result<domain::Folder> save(const domain::Folder& folder) = 0;
  [[nodiscard]] virtual Result<void> remove(const domain::FolderId& id) = 0;
};

}  // namespace notes::application
