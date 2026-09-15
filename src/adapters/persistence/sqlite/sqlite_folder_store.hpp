#pragma once

#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "application/ports/folders/folder_reader.hpp"
#include "application/ports/folders/folder_writer.hpp"

#include <memory>

namespace notes::adapters::persistence {

class SqliteFolderStore final : public application::FolderReader,
                                public application::FolderWriter {
public:
  explicit SqliteFolderStore(std::shared_ptr<SqliteDb> db);

  [[nodiscard]] application::Result<domain::Folder> load(
      const domain::FolderId& id) const override;

  [[nodiscard]] application::Result<std::vector<domain::Folder>> list_all()
      const override;

  [[nodiscard]] application::Result<domain::Folder> save(
      const domain::Folder& folder) override;

  [[nodiscard]] application::Result<void> remove(
      const domain::FolderId& id) override;

private:
  std::shared_ptr<SqliteDb> db_;
};

}  // namespace notes::adapters::persistence
