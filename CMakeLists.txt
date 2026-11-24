cmake_minimum_required(VERSION 3.16)
file(READ "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER)
string(STRIP "${PROJECT_VER}" PROJECT_VER)
# Run the configure step again when version.txt changes; otherwise the old number stays in the firmware.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/version.txt")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(loradeck)
