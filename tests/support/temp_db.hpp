#pragma once

#include <filesystem>
#include <string>
#include <utility>

namespace notes::testing {

// RAII temp SQLite path: removes db + WAL/SHM on destroy.
class TempDbPath {
public:
  explicit TempDbPath(const char* name) {
    dir_ = std::filesystem::temp_directory_path() / "linux-notes-tests";
    std::filesystem::create_directories(dir_);
    path_ = dir_ / name;
    cleanup_files();
  }

  TempDbPath(const TempDbPath&) = delete;
  TempDbPath& operator=(const TempDbPath&) = delete;

  TempDbPath(TempDbPath&& other) noexcept
      : dir_(std::move(other.dir_)), path_(std::move(other.path_)) {
    other.path_.clear();
  }

  TempDbPath& operator=(TempDbPath&& other) noexcept {
    if (this != &other) {
      cleanup_files();
      dir_ = std::move(other.dir_);
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }

  ~TempDbPath() { cleanup_files(); }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  [[nodiscard]] std::string string() const { return path_.string(); }

private:
  void cleanup_files() {
    if (path_.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    std::filesystem::remove(std::filesystem::path(path_.string() + "-wal"), ec);
    std::filesystem::remove(std::filesystem::path(path_.string() + "-shm"), ec);
  }

  std::filesystem::path dir_;
  std::filesystem::path path_;
};

}  // namespace notes::testing
