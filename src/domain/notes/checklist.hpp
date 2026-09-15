#pragma once
#include <string>
#include <utility>
#include <vector>

namespace notes::domain {

struct ChecklistItem {
  bool done{false};
  std::string text;
};

class ChecklistBlock {
public:
  ChecklistBlock() = default;
  explicit ChecklistBlock(std::vector<ChecklistItem> items) : items_(std::move(items)) {}

  [[nodiscard]] const std::vector<ChecklistItem>& items() const noexcept { return items_; }

  [[nodiscard]] ChecklistBlock with_toggled(std::size_t index) const {
    auto copy = items_;
    if (index < copy.size()) {
      copy[index].done = !copy[index].done;
    }
    return ChecklistBlock{std::move(copy)};
  }

  [[nodiscard]] ChecklistBlock with_item_text(std::size_t index, std::string text) const {
    auto copy = items_;
    if (index < copy.size()) {
      copy[index].text = std::move(text);
    }
    return ChecklistBlock{std::move(copy)};
  }

private:
  std::vector<ChecklistItem> items_;
};

}  // namespace notes::domain
