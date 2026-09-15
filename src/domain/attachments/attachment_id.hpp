#pragma once
#include <string>
#include <utility>

namespace notes::domain {

// Attachment blob ids are opaque tokens used both as note-content references and
// as on-disk file stem names under the attachments root. Untrusted input
// (rich-document markers, anchors, codec rows) must be rejected before any
// filesystem join: separators, traversal segments, absolute forms, and empty
// values are never valid. Domain "non-empty string" is intentionally not enough.
class AttachmentId {
public:
  AttachmentId() = default;
  explicit AttachmentId(std::string value) : value_(std::move(value)) {}

  [[nodiscard]] const std::string& value() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

  // Safe opaque stem: att-prefixed, charset limited to [A-Za-z0-9_-], no
  // separators or "."/".." path elements. Matches put() id shape (att-{ms}-{n})
  // and in-memory test ids (att-mem-*). Legacy markers without the att- prefix
  // are not accepted for FS operations — they must be treated as plain text.
  [[nodiscard]] static bool is_opaque_safe(const std::string& raw) noexcept {
    if (raw.size() < 5) {
      return false;
    }
    if (raw.compare(0, 4, "att-") != 0) {
      return false;
    }
    for (std::size_t i = 4; i < raw.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(raw[i]);
      const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-';
      if (!ok) {
        return false;
      }
    }
    // Guard against "att-.." / "att-.-x" style oddities even within charset.
    if (raw.find("..") != std::string::npos) {
      return false;
    }
    return true;
  }

  [[nodiscard]] bool is_opaque_safe() const noexcept {
    return is_opaque_safe(value_);
  }

  friend bool operator==(const AttachmentId& a, const AttachmentId& b) noexcept {
    return a.value_ == b.value_;
  }
  friend bool operator!=(const AttachmentId& a, const AttachmentId& b) noexcept {
    return !(a == b);
  }
  friend bool operator<(const AttachmentId& a, const AttachmentId& b) noexcept {
    return a.value_ < b.value_;
  }

private:
  std::string value_;
};

}  // namespace notes::domain
