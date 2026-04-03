# Required CMake Version
cmake_minimum_required(VERSION 3.16)

# Load credentials from ~/.esp_creds (WiFi, GitHub token) — must be before project.cmake
list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "$ENV{HOME}/.esp_creds")
    list(APPEND SDKCONFIG_DEFAULTS "$ENV{HOME}/.esp_creds")
endif()

# Set up ESP-IDF build environment
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project name
project(gh-stats-dashboard)

