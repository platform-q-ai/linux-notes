add_library(notes_adapters_sqlite
  sqlite_db.cpp
  sqlite_note_store.cpp
  sqlite_folder_store.cpp
)
add_library(notes::adapters_sqlite ALIAS notes_adapters_sqlite)

target_include_directories(notes_adapters_sqlite PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
target_link_libraries(notes_adapters_sqlite
  PUBLIC notes_app_ports notes_domain
  PRIVATE SQLite::SQLite3
)
target_compile_features(notes_adapters_sqlite PUBLIC cxx_std_20
)
