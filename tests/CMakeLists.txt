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

add_executable(test_notes_keep_both_attachment
  application/use_cases/keep_both_attachment_test.cpp)
target_link_libraries(test_notes_keep_both_attachment PRIVATE
  notes_app notes_test_support)
add_test(NAME test_notes_keep_both_attachment
         COMMAND test_notes_keep_both_attachment)

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

  # Close/quit lifecycle: AppExitGate vetoes normal close on failed flushSync.
  add_executable(test_notes_close_lifecycle presentation/test_close_lifecycle.cpp)
  target_include_directories(test_notes_close_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(test_notes_close_lifecycle PRIVATE
    notes_presentation notes_app notes_test_support notes_catch2
    Qt6::Core Qt6::Test)
  add_test(NAME test_notes_close_lifecycle COMMAND test_notes_close_lifecycle)
  set_tests_properties(test_notes_close_lifecycle PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")

  # Structured content mapper: checklist/attachment roundtrip fidelity.
  add_executable(test_notes_content_mapper presentation/test_note_content_mapper.cpp)
  target_include_directories(test_notes_content_mapper PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(test_notes_content_mapper PRIVATE
    notes_presentation notes_app notes_test_support notes_catch2
    Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME test_notes_content_mapper COMMAND test_notes_content_mapper)
  set_tests_properties(test_notes_content_mapper PROPERTIES
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


add_executable(test_notes_migration adapters/migration_test.cpp)
target_link_libraries(test_notes_migration PRIVATE
  notes_adapters_sqlite notes_test_support)
add_test(NAME test_notes_migration COMMAND test_notes_migration)

add_executable(test_notes_conflict_ids application/use_cases/conflict_id_test.cpp)
target_link_libraries(test_notes_conflict_ids PRIVATE notes_app notes_test_support)
add_test(NAME test_notes_conflict_ids COMMAND test_notes_conflict_ids)

# Persistence + toggle structured content roundtrip (Task 8 helper / regression).
add_executable(test_notes_structured_roundtrip adapters/structured_roundtrip_test.cpp)
target_link_libraries(test_notes_structured_roundtrip PRIVATE
  notes_adapters_sqlite notes_app notes_test_support)
add_test(NAME test_notes_structured_roundtrip COMMAND test_notes_structured_roundtrip)

# Soft-delete trash / restore / purge + move (Task 4).
add_executable(test_notes_trash adapters/trash_restore_purge_test.cpp)
target_link_libraries(test_notes_trash PRIVATE
  notes_adapters_sqlite notes_app notes_test_support)
add_test(NAME test_notes_trash COMMAND test_notes_trash)

add_executable(test_notes_trash_uc application/use_cases/trash_note_test.cpp)
target_link_libraries(test_notes_trash_uc PRIVATE notes_app notes_test_support)
add_test(NAME test_notes_trash_uc COMMAND test_notes_trash_uc)

if(TARGET notes_presentation)
  add_executable(test_notes_editor_attach_checklist
    presentation/test_editor_attach_checklist.cpp)
  target_include_directories(test_notes_editor_attach_checklist PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(test_notes_editor_attach_checklist PRIVATE
    notes_presentation notes_app notes_adapters_attachments notes_test_support
    notes_catch2 Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME test_notes_editor_attach_checklist
           COMMAND test_notes_editor_attach_checklist)
  set_tests_properties(test_notes_editor_attach_checklist PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/presentation/test_rapid_switch.cpp)
    add_executable(test_notes_rapid_switch presentation/test_rapid_switch.cpp)
    target_include_directories(test_notes_rapid_switch PRIVATE
      ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(test_notes_rapid_switch PRIVATE
      notes_presentation notes_app notes_test_support notes_catch2
      Qt6::Core Qt6::Test)
    add_test(NAME test_notes_rapid_switch COMMAND test_notes_rapid_switch)
    set_tests_properties(test_notes_rapid_switch PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
  endif()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/presentation/test_search_list_pin_coherence.cpp)
    add_executable(test_notes_search_list_pin_coherence
      presentation/test_search_list_pin_coherence.cpp)
    target_include_directories(test_notes_search_list_pin_coherence PRIVATE
      ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(test_notes_search_list_pin_coherence PRIVATE
      notes_presentation notes_app notes_test_support notes_catch2
      Qt6::Core Qt6::Test)
    add_test(NAME test_notes_search_list_pin_coherence
             COMMAND test_notes_search_list_pin_coherence)
    set_tests_properties(test_notes_search_list_pin_coherence PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
  endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/adapters/trash_lifecycle_sqlite_test.cpp)
  add_executable(test_notes_trash_lifecycle_sqlite
    adapters/trash_lifecycle_sqlite_test.cpp)
  target_link_libraries(test_notes_trash_lifecycle_sqlite PRIVATE
    notes_adapters_sqlite notes_app notes_test_support)
  add_test(NAME test_notes_trash_lifecycle_sqlite
           COMMAND test_notes_trash_lifecycle_sqlite)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/adapters/migration_v3_integrity_test.cpp)
  add_executable(test_notes_migration_v3_integrity
    adapters/migration_v3_integrity_test.cpp)
  target_link_libraries(test_notes_migration_v3_integrity PRIVATE
    notes_adapters_sqlite notes_test_support)
  add_test(NAME test_notes_migration_v3_integrity
           COMMAND test_notes_migration_v3_integrity)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/adapters/filesystem_attachment_store_test.cpp)
  add_executable(test_notes_filesystem_attachment_store
    adapters/filesystem_attachment_store_test.cpp)
  target_link_libraries(test_notes_filesystem_attachment_store PRIVATE
    notes_adapters_attachments notes_test_support)
  add_test(NAME test_notes_filesystem_attachment_store
           COMMAND test_notes_filesystem_attachment_store)
endif()

# PR#4 remediation: trash resurrection, checklist plain/doc offsets, flush reentrancy.
if(TARGET notes_presentation)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/presentation/test_trash_editor_lifecycle.cpp)
    add_executable(test_notes_trash_editor_lifecycle
      presentation/test_trash_editor_lifecycle.cpp)
    target_include_directories(test_notes_trash_editor_lifecycle PRIVATE
      ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(test_notes_trash_editor_lifecycle PRIVATE
      notes_presentation notes_app notes_test_support notes_catch2
      Qt6::Core Qt6::Test)
    add_test(NAME test_notes_trash_editor_lifecycle
             COMMAND test_notes_trash_editor_lifecycle)
    set_tests_properties(test_notes_trash_editor_lifecycle PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
  endif()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/presentation/test_checklist_offset_mapping.cpp)
    add_executable(test_notes_checklist_offset_mapping
      presentation/test_checklist_offset_mapping.cpp)
    target_include_directories(test_notes_checklist_offset_mapping PRIVATE
      ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(test_notes_checklist_offset_mapping PRIVATE
      notes_presentation notes_app notes_test_support notes_catch2
      Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME test_notes_checklist_offset_mapping
             COMMAND test_notes_checklist_offset_mapping)
    set_tests_properties(test_notes_checklist_offset_mapping PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
  endif()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/presentation/test_flush_reentrancy.cpp)
    add_executable(test_notes_flush_reentrancy
      presentation/test_flush_reentrancy.cpp)
    target_include_directories(test_notes_flush_reentrancy PRIVATE
      ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(test_notes_flush_reentrancy PRIVATE
      notes_presentation notes_app notes_test_support notes_catch2
      Qt6::Core Qt6::Test)
    add_test(NAME test_notes_flush_reentrancy
             COMMAND test_notes_flush_reentrancy)
    set_tests_properties(test_notes_flush_reentrancy PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
  endif()
endif()
