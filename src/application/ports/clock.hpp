#pragma once
#include <cstdint>

namespace notes::application {

class Clock {
public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual std::int64_t now_ms() const = 0;
};

}  // namespace notes::application
