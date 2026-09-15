add_library(notes_adapters_attachments
  filesystem_attachment_store.cpp
)
add_library(notes::adapters_attachments ALIAS notes_adapters_attachments)

target_include_directories(notes_adapters_attachments PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(notes_adapters_attachments
  PUBLIC notes_app_ports notes_domain
)
target_compile_features(notes_adapters_attachments PUBLIC cxx_std_20)
