add_library(notes_adapters_system
  xdg_storage_paths.cpp
)
add_library(notes::adapters_system ALIAS notes_adapters_system)

target_include_directories(notes_adapters_system PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(notes_adapters_system
  PUBLIC notes_app_ports
)
target_compile_features(notes_adapters_system PUBLIC cxx_std_20)
