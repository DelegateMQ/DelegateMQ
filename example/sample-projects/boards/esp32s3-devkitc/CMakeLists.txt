# boards/esp32s3-devkitc: DelegateMQ on an ESP32-S3-DevKitC-1 (N8R8) under
# ESP-IDF's FreeRTOS. An ESP-IDF project: PlatformIO builds it (platformio.ini),
# and so does idf.py:
#
#   . $IDF_PATH/export.sh
#   idf.py set-target esp32s3      (once)
#   idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "boards/esp32s3-devkitc is an ESP-IDF project: build it with PlatformIO, or run ESP-IDF's export.sh first (IDF_PATH isn't set).")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(delegate_esp32s3)
