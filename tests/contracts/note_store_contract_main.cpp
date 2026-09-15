#include "tests/contracts/note_store_contract.hpp"
#include "tests/support/fakes/in_memory_note_store.hpp"

#include "adapters/persistence/sqlite/sqlite_db.hpp"
#include "adapters/persistence/sqlite/sqlite_note_store.hpp"

#include <filesystem>
#include <iostream>
#include <memory>

int main() {
  try {
    {
      notes::testing::InMemoryNoteStore mem;
      notes::tests::run_note_store_contract(mem, "InMemoryNoteStore");
    }
    {
      const auto dir =
          std::filesystem::temp_directory_path() / "linux-notes-contract";
      std::filesystem::create_directories(dir);
      const auto db_path = dir / "contract.db";
      std::error_code ec;
      std::filesystem::remove(db_path, ec);

      auto db = std::make_shared<notes::adapters::persistence::SqliteDb>();
      auto opened = db->open(db_path);
      if (!opened) {
        std::cerr << "sqlite open failed: " << opened.error().message << "\n";
        return 2;
      }
      notes::adapters::persistence::SqliteNoteStore store(db);
      notes::tests::run_note_store_contract(store, "SqliteNoteStore");
    }
  } catch (const std::exception& ex) {
    std::cerr << "CONTRACT FAIL: " << ex.what() << "\n";
    return 1;
  }
  std::cerr << "ALL NOTE STORE CONTRACTS PASSED\n";
  return 0;
}
