#pragma once
#include "application/ports/clock.hpp"

#include <cstdint>

namespace notes::testing {

class FixedClock final : public application::Clock {
public:
  explicit FixedClock(std::int64_t now_ms = 1'000'000) : now_ms_(now_ms) {}

  [[nodiscard]] std::int64_t now_ms() const override { return now_ms_; }
  void advance(std::int64_t delta_ms) { now_ms_ += delta_ms; }
  void set(std::int64_t now_ms) { now_ms_ = now_ms; }

private:
  mutable std::int64_t now_ms_;
};

}  // namespace notes::testing
