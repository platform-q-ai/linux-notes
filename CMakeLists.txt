cmake_minimum_required(VERSION 3.21)
project(linux-notes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(NOTES_ENABLE_SANITIZERS "Enable ASan/UBSan" OFF)
option(NOTES_BUILD_TESTS "Build tests" ON)
option(NOTES_BUILD_UI "Build Qt/QML presentation and desktop" ON)

if(NOTES_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(SQLite3 REQUIRED)

set(NOTES_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(notes_domain INTERFACE)
add_library(notes::domain ALIAS notes_domain)
target_include_directories(notes_domain INTERFACE ${NOTES_SRC})
target_compile_features(notes_domain INTERFACE cxx_std_20)

add_library(notes_app_ports INTERFACE)
add_library(notes::app_ports ALIAS notes_app_ports)
target_link_libraries(notes_app_ports INTERFACE notes_domain)
target_include_directories(notes_app_ports INTERFACE ${NOTES_SRC})

add_library(notes_app INTERFACE)
add_library(notes::app ALIAS notes_app)
target_link_libraries(notes_app INTERFACE notes_app_ports)
target_include_directories(notes_app INTERFACE ${NOTES_SRC})

add_library(notes_adapters_sqlite
  src/adapters/persistence/sqlite/sqlite_db.cpp
  src/adapters/persistence/sqlite/sqlite_note_store.cpp
  src/adapters/persistence/sqlite/sqlite_folder_store.cpp
)
add_library(notes::adapters_sqlite ALIAS notes_adapters_sqlite)
target_include_directories(notes_adapters_sqlite PUBLIC ${NOTES_SRC})
target_link_libraries(notes_adapters_sqlite PUBLIC notes_app_ports SQLite::SQLite3)
target_compile_features(notes_adapters_sqlite PUBLIC cxx_std_20)

add_library(notes_adapters_attachments
  src/adapters/attachments/filesystem_attachment_store.cpp
)
add_library(notes::adapters_attachments ALIAS notes_adapters_attachments)
target_include_directories(notes_adapters_attachments PUBLIC ${NOTES_SRC})
target_link_libraries(notes_adapters_attachments PUBLIC notes_app_ports)
target_compile_features(notes_adapters_attachments PUBLIC cxx_std_20)

add_library(notes_adapters_system INTERFACE)
add_library(notes::adapters_system ALIAS notes_adapters_system)
target_include_directories(notes_adapters_system INTERFACE ${NOTES_SRC})
target_link_libraries(notes_adapters_system INTERFACE notes_app_ports)

set(NOTES_HAVE_QT OFF)
if(NOTES_BUILD_UI)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
  set(NOTES_HAVE_QT ON)

  add_library(notes_presentation
    src/presentation/qt/execution/use_case_dispatcher.cpp
    src/presentation/qt/execution/use_case_dispatcher.hpp
    src/presentation/qt/app_exit_gate.cpp
    src/presentation/qt/app_exit_gate.hpp
    src/presentation/qt/models/folder_tree_model.cpp
    src/presentation/qt/models/folder_tree_model.hpp
    src/presentation/qt/models/note_list_model.cpp
    src/presentation/qt/models/note_list_model.hpp
    src/presentation/qt/mapping/note_content_document_mapper.cpp
    src/presentation/qt/mapping/note_content_document_mapper.hpp
    src/presentation/qt/view_models/folder_tree_view_model.cpp
    src/presentation/qt/view_models/folder_tree_view_model.hpp
    src/presentation/qt/view_models/note_list_view_model.cpp
    src/presentation/qt/view_models/note_list_view_model.hpp
    src/presentation/qt/view_models/editor_view_model.cpp
    src/presentation/qt/view_models/editor_view_model.hpp
    src/presentation/qml/presentation_qml.qrc
  )
  add_library(notes::presentation ALIAS notes_presentation)
  target_include_directories(notes_presentation PUBLIC ${NOTES_SRC})
  target_compile_features(notes_presentation PUBLIC cxx_std_20)
  target_link_libraries(notes_presentation PUBLIC
    notes_app Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)

  qt_add_executable(notes
    src/desktop/main.cpp
    # Ensure QRC symbols are linked (static lib may drop them)
    src/presentation/qml/presentation_qml.qrc
  )
  set(NOTES_QML_DIR ${NOTES_SRC}/presentation/qml)
  target_compile_definitions(notes PRIVATE NOTES_QML_DIR="${NOTES_QML_DIR}")
  target_include_directories(notes PRIVATE ${NOTES_SRC} ${NOTES_SRC}/desktop)
  target_link_libraries(notes PRIVATE
    notes_domain notes_app
    notes_adapters_sqlite notes_adapters_attachments notes_adapters_system
    notes_presentation
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
  )
  install(TARGETS notes RUNTIME DESTINATION bin)
endif()

if(NOTES_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
