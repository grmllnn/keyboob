cmake_minimum_required(VERSION 3.20)
project(keyboop-linux LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

option(KEYBOOP_BUILD_FCITX "Build Fcitx5 addon" ON)
option(KEYBOOP_BUILD_TESTS "Build assert-based tests" ON)

# Prefer system nlohmann-json; fall back to vendored header.
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  add_library(nlohmann_json INTERFACE)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
  target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  message(STATUS "Using vendored nlohmann/json.hpp")
endif()

set(KEYBOOP_CORE_SOURCES
  src/core/keymap.cpp
  src/core/layout_data.cpp
  src/core/extra_words.cpp
  src/core/exception_store.cpp
  src/core/layout_detector.cpp
  src/core/ambiguous_pairs.cpp
  src/core/keystroke_buffer.cpp
  src/core/anti_resonance.cpp
  src/core/snippet_store.cpp
  src/core/engine.cpp
  src/core/undo_learner.cpp
)

add_library(keyboop_core STATIC ${KEYBOOP_CORE_SOURCES})
target_include_directories(keyboop_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_link_libraries(keyboop_core PUBLIC nlohmann_json::nlohmann_json)

# Default data dir for tests / runtime fallback
set(KEYBOOP_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data" CACHE PATH
  "Path to trigrams_*.json and words_*.json")
target_compile_definitions(keyboop_core PUBLIC
  KEYBOOP_DEFAULT_DATA_DIR="${KEYBOOP_DATA_DIR}")

if(KEYBOOP_BUILD_TESTS)
  enable_testing()
  add_executable(keyboop_core_tests tests/test_core.cpp)
  target_link_libraries(keyboop_core_tests PRIVATE keyboop_core)
  add_test(NAME keyboop_core_tests COMMAND keyboop_core_tests)
  set_tests_properties(keyboop_core_tests PROPERTIES
    ENVIRONMENT "KEYBOOP_DATA_DIR=${KEYBOOP_DATA_DIR}")
endif()

add_executable(keyboopctl src/ctl/keyboopctl.cpp)
target_link_libraries(keyboopctl PRIVATE keyboop_core)

# --- Fcitx5 addon (optional) ---
if(KEYBOOP_BUILD_FCITX)
  find_package(Fcitx5Core QUIET)
  find_package(Fcitx5Module QUIET)
  find_package(PkgConfig QUIET)
  if(Fcitx5Core_FOUND)
    add_library(keyboop MODULE src/fcitx5/keyboop_addon.cpp)
    target_link_libraries(keyboop PRIVATE keyboop_core Fcitx5::Core)
    target_include_directories(keyboop PRIVATE src/fcitx5)
    set_target_properties(keyboop PROPERTIES PREFIX "" OUTPUT_NAME "keyboop")

    # Install paths follow Fcitx5 conventions when available
    if(TARGET Fcitx5::Core)
      include(GNUInstallDirs)
      if(DEFINED FCITX_INSTALL_ADDONDIR)
        set(_addon_dir "${FCITX_INSTALL_ADDONDIR}")
      else()
        set(_addon_dir "${CMAKE_INSTALL_LIBDIR}/fcitx5")
      endif()
      install(TARGETS keyboop DESTINATION ${_addon_dir})
      install(FILES src/fcitx5/keyboop.conf
              DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/fcitx5/addon")
      install(FILES src/fcitx5/keyboop-config.desc
              DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/fcitx5/configdesc"
              RENAME keyboop.desc)
    endif()
    message(STATUS "Fcitx5 addon: ENABLED")
  else()
    message(STATUS "Fcitx5 not found — addon skipped (core + tests still build)")
  endif()
endif()

install(TARGETS keyboopctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${KEYBOOP_DATA_DIR}/
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/keyboop
        FILES_MATCHING PATTERN "*.json")
