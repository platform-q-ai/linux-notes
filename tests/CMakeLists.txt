add_library(notes_test_support INTERFACE)
add_library(notes::test_support ALIAS notes_test_support)
target_include_directories(notes_test_support INTERFACE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
)
target_link_libraries(notes_test_support INTERFACE notes_app_ports notes_domain)

set(NOTES_CATCH2_DIR ${CMAKE_SOURCE_DIR}/third_party/catch2)
add_library(notes_catch2 STATIC ${NOTES_CATCH2_DIR}/catch_amalgamated.cpp)
target_include_directories(notes_catch2 PUBLIC ${NOTES_CATCH2_DIR})
target_compile_features(notes_catch2 PUBLIC cxx_std_20)

add_executable(test_notes_domain domain/note_content_test.cpp)
target_link_libraries(test_notes_domain PRIVATE notes_domain notes_test_support)
add_test(NAME test_notes_domain COMMAND test_notes_domain)

add_executable(test_notes_save_note application/use_cases/save_note_test.cpp)
target_link_libraries(test_notes_save_note PRIVATE notes_app notes_test_support)
add_test(NAME test_notes_save_note COMMAND test_notes_save_note)

add_executable(test_notes_store_contract contracts/note_store_contract_main.cpp)
target_link_libraries(test_notes_store_contract PRIVATE notes_adapters_sqlite notes_test_support)
add_test(NAME test_notes_store_contract COMMAND test_notes_store_contract)

# Catch2 contract suite (provides its own CATCH_CONFIG via catch lib? need main)
add_executable(test_notes_store_contract_catch
  contracts/note_store_contract.cpp
  catch_main.cpp
)
target_link_libraries(test_notes_store_contract_catch PRIVATE
  notes_catch2 notes_adapters_sqlite notes_test_support)
add_test(NAME test_notes_store_contract_catch COMMAND test_notes_store_contract_catch)

if(TARGET notes_presentation)
  find_package(Qt6 REQUIRED COMPONENTS Core Test)
  add_executable(test_notes_presentation presentation/test_editor_debounce.cpp)
  target_include_directories(test_notes_presentation PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(test_notes_presentation PRIVATE
    notes_presentation notes_app notes_test_support notes_catch2
    Qt6::Core Qt6::Test)
  add_test(NAME test_notes_presentation COMMAND test_notes_presentation)
  set_tests_properties(test_notes_presentation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
endif()

add_executable(test_notes_content_codec adapters/content_codec_test.cpp)
target_link_libraries(test_notes_content_codec PRIVATE
  notes_adapters_sqlite notes_domain notes_test_support)
add_test(NAME test_notes_content_codec COMMAND test_notes_content_codec)

add_executable(test_notes_folder_integrity adapters/folder_integrity_test.cpp)
target_link_libraries(test_notes_folder_integrity PRIVATE
  notes_adapters_sqlite notes_test_support)
add_test(NAME test_notes_folder_integrity COMMAND test_notes_folder_integrity)

add_executable(test_notes_search_failclosed adapters/search_index_failclosed_test.cpp)
target_link_libraries(test_notes_search_failclosed PRIVATE
  notes_adapters_sqlite notes_test_support)
add_test(NAME test_notes_search_failclosed COMMAND test_notes_search_failclosed)
