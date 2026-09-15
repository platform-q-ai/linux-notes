#pragma once

#include "application/ports/clock.hpp"

#include <chrono>

namespace notes::adapters::system {

class SystemClock final : public application::Clock {
public:
  [[nodiscard]] std::int64_t now_ms() const override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }
};

}  // namespace notes::adapters::system
