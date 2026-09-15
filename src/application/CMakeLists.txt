# Ports + header-only use cases.
add_library(notes_app_ports INTERFACE)
add_library(notes::app_ports ALIAS notes_app_ports)
target_include_directories(notes_app_ports INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(notes_app_ports INTERFACE notes_domain)
target_compile_features(notes_app_ports INTERFACE cxx_std_20)

# notes_app is an INTERFACE alias over ports (UCs live in headers).
add_library(notes_app INTERFACE)
add_library(notes::app ALIAS notes_app)
target_link_libraries(notes_app INTERFACE notes_app_ports notes_domain)
target_include_directories(notes_app INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(notes_app INTERFACE cxx_std_20)
