cmake_minimum_required(VERSION 3.16)

# Board selection: BOARD=cyd28 idf build   (default: p4)
# Use env var — cmake -D doesn't propagate into IDF component scopes.
if(DEFINED ENV{BOARD})
    set(BOARD "$ENV{BOARD}")
else()
    set(BOARD "p4")
endif()
message(STATUS "ROOT: BOARD='${BOARD}'")
# Write board selection to a file so component CMakeLists (separate cmake process) can read it
file(WRITE "${CMAKE_SOURCE_DIR}/board_config.cmake" "set(BOARD \"${BOARD}\")\n")

# Swap in the right component manifest before IDF component manager reads it.
# idf_component.yml is board-specific — overwritten at configure time.
if(BOARD STREQUAL "cyd28")
    file(READ ${CMAKE_SOURCE_DIR}/main/idf_component.cyd28.yml _manifest)
else()
    file(READ ${CMAKE_SOURCE_DIR}/main/idf_component.p4.yml _manifest)
endif()
file(WRITE ${CMAKE_SOURCE_DIR}/main/idf_component.yml "${_manifest}")

# Load credentials from ~/.esp_creds (WiFi, GitHub token) — must be before project.cmake
if(BOARD STREQUAL "cyd28")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.cyd28")
else()
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()
if(EXISTS "$ENV{HOME}/.esp_creds")
    list(APPEND SDKCONFIG_DEFAULTS "$ENV{HOME}/.esp_creds")
endif()

# Set up ESP-IDF build environment
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project name
project(esp32-gh-dashboard)
