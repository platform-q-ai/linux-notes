#pragma once
#include "application/result.hpp"
#include "domain/folders/folder.hpp"
#include "domain/folders/folder_id.hpp"

#include <vector>

namespace notes::application {

class FolderReader {
public:
  virtual ~FolderReader() = default;
  [[nodiscard]] virtual Result<domain::Folder> load(const domain::FolderId& id) const = 0;
  [[nodiscard]] virtual Result<std::vector<domain::Folder>> list_all() const = 0;
};

}  // namespace notes::application
