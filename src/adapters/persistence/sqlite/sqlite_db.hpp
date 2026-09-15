#pragma once

#include "application/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace notes::adapters::persistence {

class SqliteDb {
public:
  SqliteDb() = default;
  ~SqliteDb();

  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;
  SqliteDb(SqliteDb&&) noexcept;
  SqliteDb& operator=(SqliteDb&&) noexcept;

  [[nodiscard]] application::Result<void> open(
      const std::filesystem::path& path);
  [[nodiscard]] application::Result<void> exec(const std::string& sql);
  [[nodiscard]] application::Result<void> migrate();

  [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

  [[nodiscard]] application::Result<void> begin_immediate();
  [[nodiscard]] application::Result<void> commit();
  [[nodiscard]] application::Result<void> rollback();

  [[nodiscard]] std::string last_error() const;

private:
  sqlite3* db_{nullptr};
};

class Stmt {
public:
  Stmt() = default;
  Stmt(sqlite3* db, const char* sql);
  ~Stmt();

  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
  Stmt(Stmt&& other) noexcept;
  Stmt& operator=(Stmt&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept { return stmt_ != nullptr; }
  [[nodiscard]] sqlite3_stmt* get() const noexcept { return stmt_; }

  void reset();
  void bind_text(int idx, const std::string& v);
  void bind_int64(int idx, std::int64_t v);
  void bind_blob(int idx, const std::string& v);
  void bind_null(int idx);

  [[nodiscard]] int step();
  [[nodiscard]] std::string column_text(int idx) const;
  [[nodiscard]] std::int64_t column_int64(int idx) const;
  [[nodiscard]] bool column_is_null(int idx) const;

private:
  sqlite3_stmt* stmt_{nullptr};
};

}  // namespace notes::adapters::persistence
