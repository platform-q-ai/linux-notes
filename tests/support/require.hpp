#pragma once

#include <stdexcept>
#include <string>

namespace notes::testing {

inline void require(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}

inline void require(bool cond, const std::string& msg) {
  if (!cond) throw std::runtime_error(msg);
}

}  // namespace notes::testing
