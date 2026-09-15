#pragma once
#include "application/ports/folders/folder_reader.hpp"
#include "application/result.hpp"
#include "domain/folders/folder.hpp"

#include <vector>

namespace notes::application {

class ListFolders {
public:
  explicit ListFolders(FolderReader& reader) : reader_(reader) {}

  [[nodiscard]] Result<std::vector<domain::Folder>> execute() const { return reader_.list_all(); }

private:
  FolderReader& reader_;
};

}  // namespace notes::application
