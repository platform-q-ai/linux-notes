#pragma once
#include "folder_id.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace notes::domain {

struct Folder {
  FolderId id;
  std::string name;
  std::optional<FolderId> parent_id;
  std::int64_t sort_order{0};
  std::int64_t created_at_ms{0};
  std::int64_t modified_at_ms{0};
};

}  // namespace notes::domain
