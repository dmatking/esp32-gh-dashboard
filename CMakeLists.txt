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
elseif(BOARD STREQUAL "cyd28s3")
    file(READ ${CMAKE_SOURCE_DIR}/main/idf_component.cyd28s3.yml _manifest)
elseif(BOARD STREQUAL "cyd35")
    file(READ ${CMAKE_SOURCE_DIR}/main/idf_component.cyd35.yml _manifest)
elseif(BOARD STREQUAL "wvshr200")
    file(READ ${CMAKE_SOURCE_DIR}/main/idf_component.wvshr200.yml _manifest)
else()
    file(READ ${CMAKE_SOURCE_DIR}/main/idf_component.p4.yml _manifest)
endif()
file(WRITE ${CMAKE_SOURCE_DIR}/main/idf_component.yml "${_manifest}")

if(BOARD STREQUAL "cyd28")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.cyd28")
elseif(BOARD STREQUAL "cyd28s3")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.cyd28s3")
elseif(BOARD STREQUAL "cyd35")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.cyd35")
elseif(BOARD STREQUAL "wvshr200")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.wvshr200")
else()
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()

# Set up ESP-IDF build environment
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project name
project(esp32-gh-dashboard)
