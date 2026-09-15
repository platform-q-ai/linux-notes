# notes_presentation — Qt/QML incoming adapter (no concrete stores)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(notes_presentation
  qt/execution/use_case_dispatcher.cpp
  qt/app_exit_gate.cpp
  qt/models/folder_tree_model.cpp
  qt/models/note_list_model.cpp
  qt/mapping/note_content_document_mapper.cpp
  qt/view_models/folder_tree_view_model.cpp
  qt/view_models/note_list_view_model.cpp
  qt/view_models/editor_view_model.cpp
)

add_library(notes::presentation ALIAS notes_presentation)

target_include_directories(notes_presentation
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(notes_presentation PUBLIC cxx_std_20)

target_link_libraries(notes_presentation
  PUBLIC
    notes_domain
    notes_app
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

# QML is owned by the desktop target via qt_add_qml_module (see root CMakeLists).
