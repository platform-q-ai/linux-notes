#pragma once

#include "application/ports/id_source.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace notes::adapters::system {

// Process-local CSPRNG token source (Qt-free). Suitable as default identity.
class RandomIdSource final : public application::IdSource {
public:
  [[nodiscard]] std::string next_unique_token() override {
    thread_local std::random_device rd;
    // seed_seq must be an lvalue for mt19937_64's non-const reference ctor.
    thread_local std::seed_seq seed{rd(), rd(), rd(), rd()};
    thread_local std::mt19937_64 gen{seed};
    std::array<std::uint8_t, 16> bytes{};
    for (auto& b : bytes) {
      b = static_cast<std::uint8_t>(gen() & 0xffu);
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(bytes.size() * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      out[i * 2] = kHex[(bytes[i] >> 4) & 0xf];
      out[i * 2 + 1] = kHex[bytes[i] & 0xf];
    }
    return out;
  }
};

}  // namespace notes::adapters::system
