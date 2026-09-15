#pragma once
#include <string>

namespace notes::application {

// Qt-free identity/entropy port for collision-resistant ids across processes.
class IdSource {
public:
  virtual ~IdSource() = default;
  // Opaque unique token (hex/uuid-like). Must not be empty.
  [[nodiscard]] virtual std::string next_unique_token() = 0;
};

}  // namespace notes::application
