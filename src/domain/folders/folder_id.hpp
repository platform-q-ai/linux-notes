#pragma once
#include <string>
#include <utility>

namespace notes::domain {

class FolderId {
public:
  FolderId() = default;
  explicit FolderId(std::string value) : value_(std::move(value)) {}

  [[nodiscard]] const std::string& value() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

  friend bool operator==(const FolderId& a, const FolderId& b) noexcept {
    return a.value_ == b.value_;
  }
  friend bool operator!=(const FolderId& a, const FolderId& b) noexcept { return !(a == b); }
  friend bool operator<(const FolderId& a, const FolderId& b) noexcept {
    return a.value_ < b.value_;
  }

private:
  std::string value_;
};

}  // namespace notes::domain
