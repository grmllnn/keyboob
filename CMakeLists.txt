cmake_minimum_required(VERSION 3.20)
project(keyboop-linux LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

option(KEYBOOP_BUILD_IBUS "Build IBus engine (GNOME)" ON)
option(KEYBOOP_BUILD_FCITX "Build Fcitx5 addon" OFF)
option(KEYBOOP_BUILD_TESTS "Build assert-based tests" ON)

find_package(PkgConfig REQUIRED)
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  add_library(nlohmann_json INTERFACE)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
  target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  message(STATUS "Using vendored nlohmann/json.hpp")
endif()

pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

set(KEYBOOP_CORE_SOURCES
  src/core/keymap.cpp
  src/core/active_keymap.cpp
  src/core/xkb_pair.cpp
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
set_target_properties(keyboop_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(keyboop_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_link_libraries(keyboop_core PUBLIC
  nlohmann_json::nlohmann_json
  PkgConfig::XKBCOMMON)

set(KEYBOOP_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data" CACHE PATH
  "Path to trigrams_*.json and words_*.json")
target_compile_definitions(keyboop_core PUBLIC
  KEYBOOP_DEFAULT_DATA_DIR="${KEYBOOP_DATA_DIR}")

add_library(keyboop_platform STATIC
  src/platform/gnome_sources.cpp
  src/platform/user_settings.cpp)
set_target_properties(keyboop_platform PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(keyboop_platform PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_link_libraries(keyboop_platform PUBLIC keyboop_core)

if(KEYBOOP_BUILD_TESTS)
  enable_testing()
  add_executable(keyboop_core_tests tests/test_core.cpp)
  target_link_libraries(keyboop_core_tests PRIVATE keyboop_core keyboop_platform)
  add_test(NAME keyboop_core_tests COMMAND keyboop_core_tests)
  set_tests_properties(keyboop_core_tests PROPERTIES
    ENVIRONMENT "KEYBOOP_DATA_DIR=${KEYBOOP_DATA_DIR}")
endif()

add_executable(keyboopctl src/ctl/keyboopctl.cpp)
target_link_libraries(keyboopctl PRIVATE keyboop_core keyboop_platform)

# --- IBus engine (default ON) ---
if(KEYBOOP_BUILD_IBUS)
  pkg_check_modules(IBUS IMPORTED_TARGET ibus-1.0)
  if(IBUS_FOUND)
    add_executable(ibus-engine-keyboop src/ibus/ibus_engine_main.cpp)
    target_link_libraries(ibus-engine-keyboop PRIVATE
      keyboop_core keyboop_platform PkgConfig::IBUS)
    target_compile_definitions(ibus-engine-keyboop PRIVATE
      KEYBOOP_LIBEXECDIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}")
    install(TARGETS ibus-engine-keyboop
            RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

    set(KEYBOOP_LIBEXECDIR "${CMAKE_INSTALL_FULL_LIBEXECDIR}")
    configure_file(src/ibus/keyboop.xml.in
                   ${CMAKE_CURRENT_BINARY_DIR}/keyboop.xml @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/keyboop.xml
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/ibus/component")
    message(STATUS "IBus engine: ENABLED")
  else()
    message(STATUS "IBus not found — engine skipped (install ibus-devel)")
  endif()
endif()

# --- Fcitx5 addon (optional, OFF by default) ---
if(KEYBOOP_BUILD_FCITX)
  find_package(Fcitx5Core QUIET)
  if(Fcitx5Core_FOUND)
    add_library(keyboop MODULE src/fcitx5/keyboop_addon.cpp)
    target_link_libraries(keyboop PRIVATE keyboop_core Fcitx5::Core)
    target_include_directories(keyboop PRIVATE src/fcitx5)
    set_target_properties(keyboop PROPERTIES PREFIX "" OUTPUT_NAME "keyboop")
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
    message(STATUS "Fcitx5 addon: ENABLED")
  else()
    message(STATUS "Fcitx5 not found — addon skipped")
  endif()
endif()

install(TARGETS keyboopctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${KEYBOOP_DATA_DIR}/
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/keyboop
        FILES_MATCHING PATTERN "*.json")
