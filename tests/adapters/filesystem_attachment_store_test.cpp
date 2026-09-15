#include "adapters/attachments/filesystem_attachment_store.hpp"
#include "domain/attachments/attachment_id.hpp"
#include "domain/notes/note_id.hpp"
#include "tests/support/fakes/fixed_clock.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool cond, const char* msg) {
  if (!cond) {
    throw std::runtime_error(msg);
  }
}

std::filesystem::path make_sandbox(const char* name) {
  const auto base =
      std::filesystem::temp_directory_path() / "linux-notes-att-sandbox" / name;
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base, ec);
  require(!ec, "create sandbox");
  return base;
}

void write_file(const std::filesystem::path& p, const std::string& body) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(out), "write_file open");
  out << body;
  require(static_cast<bool>(out), "write_file body");
}

std::string read_file(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  require(static_cast<bool>(in), "read_file open");
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  using notes::adapters::attachments::FilesystemAttachmentStore;
  using notes::application::ErrorKind;
  using notes::domain::AttachmentId;
  using notes::domain::NoteId;
  using notes::testing::FixedClock;

  int failures = 0;
  const auto report = [&](const char* name, bool ok) {
    if (ok) {
      std::cout << "PASS " << name << "\n";
    } else {
      std::cout << "FAIL " << name << "\n";
      ++failures;
    }
  };

  try {
    // Domain gate: separators / traversal / absolute / empty rejected.
    report("id_rejects_empty", !AttachmentId::is_opaque_safe(""));
    report("id_rejects_dotdot", !AttachmentId::is_opaque_safe("../leak"));
    report("id_rejects_slash", !AttachmentId::is_opaque_safe("att/evil"));
    report("id_rejects_abs", !AttachmentId::is_opaque_safe("/tmp/x"));
    report("id_rejects_backslash", !AttachmentId::is_opaque_safe("att\\x"));
    report("id_rejects_no_prefix", !AttachmentId::is_opaque_safe("legacy-9"));
    report("id_accepts_put_shape",
           AttachmentId::is_opaque_safe("att-1234-1"));

    const auto sandbox = make_sandbox("path-for");
    const auto root = sandbox / "attachments";
    const auto outside = sandbox / "outside";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(outside);
    const auto victim = outside / "leak.bin";
    write_file(victim, "SECRET");

    FixedClock clock{42'000};
    FilesystemAttachmentStore store(root, clock);

    // Relative traversal must not read/delete files outside the attachments root.
    {
      const AttachmentId evil{"../outside/leak"};
      auto got = store.get(evil);
      const bool blocked =
          !got && (got.error().kind == ErrorKind::ValidationFailed ||
                   got.error().kind == ErrorKind::NotFound);
      const bool intact = read_file(victim) == "SECRET";
      report("get_traversal_blocked", blocked && intact);

      auto rm = store.remove(evil);
      const bool rm_blocked =
          !rm && rm.error().kind == ErrorKind::ValidationFailed;
      // Even if remove "succeeds" vacuously, victim must remain.
      const bool still = std::filesystem::exists(victim) &&
                         read_file(victim) == "SECRET";
      report("remove_traversal_no_delete_outside",
             (rm_blocked || still) && still);
    }

    // Absolute-looking ids must not resolve off-root.
    {
      const AttachmentId abs_id{victim.string()};
      auto got = store.get(abs_id);
      report("get_absolute_id_blocked", !got);
      report("absolute_victim_intact", read_file(victim) == "SECRET");
    }

    // Happy path put/get/remove stays inside root.
    {
      std::vector<std::uint8_t> bytes{'h', 'i'};
      auto put = store.put(NoteId{"note-1"}, "a.bin", "text/plain", bytes);
      require(static_cast<bool>(put), "put ok");
      report("put_id_opaque_safe",
             AttachmentId::is_opaque_safe(put.value().id.value()));
      auto got = store.get(put.value().id);
      report("get_happy", static_cast<bool>(got) && got.value() == bytes);
      require(static_cast<bool>(store.remove(put.value().id)), "remove happy");
      auto missing = store.get(put.value().id);
      report("get_after_remove_missing",
             !missing && missing.error().kind == ErrorKind::NotFound);
    }

    // Symlink escape: a blob name that is a symlink pointing outside root.
    // FS seam must not follow it for get/remove.
    {
      std::vector<std::uint8_t> bytes{'x'};
      auto put = store.put(NoteId{"n"}, "s.bin", "", bytes);
      require(static_cast<bool>(put), "put for symlink case");
      const auto blob = root / (put.value().id.value() + ".bin");
      require(std::filesystem::remove(blob), "unlink real blob");
      std::error_code ec;
      std::filesystem::create_symlink(victim, blob, ec);
      require(!ec, "create symlink blob");

      auto got = store.get(put.value().id);
      const bool blocked = !got;
      const bool intact = read_file(victim) == "SECRET";
      report("get_symlink_escape_blocked", blocked && intact);

      auto rm = store.remove(put.value().id);
      // remove may clear the symlink entry inside root, but must not delete victim
      (void)rm;
      report("remove_symlink_keeps_outside_target",
             std::filesystem::exists(victim) && read_file(victim) == "SECRET");
    }
  } catch (const std::exception& ex) {
    std::cerr << "EXCEPTION " << ex.what() << "\n";
    return 2;
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "filesystem_attachment_store_test PASS\n";
  return 0;
}
