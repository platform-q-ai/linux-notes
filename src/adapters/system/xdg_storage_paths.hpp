#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>

namespace notes::adapters::system {

inline std::filesystem::path xdg_data_home() {
  if (const char* v = std::getenv("XDG_DATA_HOME"); v && *v) {
    return std::filesystem::path{v};
  }
  const char* home = std::getenv("HOME");
  return std::filesystem::path{home ? home : "."} / ".local" / "share";
}

inline std::filesystem::path xdg_config_home() {
  if (const char* v = std::getenv("XDG_CONFIG_HOME"); v && *v) {
    return std::filesystem::path{v};
  }
  const char* home = std::getenv("HOME");
  return std::filesystem::path{home ? home : "."} / ".config";
}

inline std::filesystem::path xdg_cache_home() {
  if (const char* v = std::getenv("XDG_CACHE_HOME"); v && *v) {
    return std::filesystem::path{v};
  }
  const char* home = std::getenv("HOME");
  return std::filesystem::path{home ? home : "."} / ".cache";
}

struct AppPaths {
  std::filesystem::path data_dir;
  std::filesystem::path config_dir;
  std::filesystem::path cache_dir;
  std::filesystem::path db_path;
  std::filesystem::path attachments_dir;
};

inline AppPaths resolve_app_paths(const std::string& app_name = "linux-notes") {
  AppPaths p;
  p.data_dir = xdg_data_home() / app_name;
  p.config_dir = xdg_config_home() / app_name;
  p.cache_dir = xdg_cache_home() / app_name;
  p.db_path = p.data_dir / "notes.db";
  p.attachments_dir = p.data_dir / "attachments";
  return p;
}

inline bool ensure_storage_dirs(const AppPaths& paths) {
  std::error_code ec;
  std::filesystem::create_directories(paths.data_dir, ec);
  if (ec) return false;
  std::filesystem::create_directories(paths.config_dir, ec);
  if (ec) return false;
  std::filesystem::create_directories(paths.cache_dir, ec);
  if (ec) return false;
  std::filesystem::create_directories(paths.attachments_dir, ec);
  return !ec;
}

}  // namespace notes::adapters::system
