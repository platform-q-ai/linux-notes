# Domain is largely header-only POD + value types.
add_library(notes_domain INTERFACE)
add_library(notes::domain ALIAS notes_domain)

target_include_directories(notes_domain INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(notes_domain INTERFACE cxx_std_20)
