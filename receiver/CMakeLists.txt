cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components/tinyusb" "${CMAKE_CURRENT_LIST_DIR}/../components/M5GFX")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stopwatch_receiver)
