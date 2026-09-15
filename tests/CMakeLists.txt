add_library(notes_test_support INTERFACE)
add_library(notes::test_support ALIAS notes_test_support)
target_include_directories(notes_test_support INTERFACE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
)
target_link_libraries(notes_test_support INTERFACE notes_app_ports notes_domain)

# Catch2 amalgamated (optional third_party)
set(NOTES_CATCH2_DIR ${CMAKE_SOURCE_DIR}/third_party/catch2)
if(EXISTS ${NOTES_CATCH2_DIR}/catch_amalgamated.cpp)
  add_library(notes_catch2 STATIC ${NOTES_CATCH2_DIR}/catch_amalgamated.cpp)
  target_include_directories(notes_catch2 PUBLIC ${NOTES_CATCH2_DIR})
  target_compile_features(notes_catch2 PUBLIC cxx_std_20)
endif()

add_executable(test_notes_domain domain/note_content_test.cpp)
target_link_libraries(test_notes_domain PRIVATE notes_domain notes_test_support)
add_test(NAME test_notes_domain COMMAND test_notes_domain)

add_executable(test_notes_save_note application/use_cases/save_note_test.cpp)
target_link_libraries(test_notes_save_note PRIVATE notes_app notes_test_support)
add_test(NAME test_notes_save_note COMMAND test_notes_save_note)

# Lightweight contract (no Catch2) — always built
add_executable(test_notes_store_contract
  contracts/note_store_contract_main.cpp
)
target_link_libraries(test_notes_store_contract
  PRIVATE notes_adapters_sqlite notes_test_support
)
add_test(NAME test_notes_store_contract COMMAND test_notes_store_contract)

# Catch2 contract suite when third_party present
if(TARGET notes_catch2)
  add_executable(test_notes_store_contract_catch
    contracts/note_store_contract.cpp
  )
  target_link_libraries(test_notes_store_contract_catch PRIVATE
    notes_catch2 notes_adapters_sqlite notes_test_support
  )
  add_test(NAME test_notes_store_contract_catch
    COMMAND test_notes_store_contract_catch)
endif()

# Presentation tests only when Qt presentation target exists
if(TARGET notes_presentation)
  find_package(Qt6 REQUIRED COMPONENTS Core Test)
  set(_pres_sources)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/presentation/test_editor_critical.cpp)
    list(APPEND _pres_sources presentation/test_editor_critical.cpp)
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/presentation/test_editor_debounce.cpp)
    list(APPEND _pres_sources presentation/test_editor_debounce.cpp)
  endif()
  if(_pres_sources)
    add_executable(test_notes_presentation ${_pres_sources})
    target_include_directories(test_notes_presentation PRIVATE
      ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(test_notes_presentation PRIVATE
      notes_presentation notes_app notes_test_support
      Qt6::Core Qt6::Test
    )
    if(TARGET notes_catch2)
      target_link_libraries(test_notes_presentation PRIVATE notes_catch2)
    endif()
    add_test(NAME test_notes_presentation COMMAND test_notes_presentation)
    set_tests_properties(test_notes_presentation PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
  endif()
endif()
