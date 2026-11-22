# Pumptron controller on the ESP32-S3-DevKitC-1 (N8R8). An ESP-IDF project:
# PlatformIO builds it (platformio.ini), and so does idf.py:
#
#   . $IDF_PATH/export.sh
#   idf.py set-target esp32s3      (once)
#   idf.py menuconfig              (Wi-Fi and the PC's address)
#   idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "Pumptron's ESP32-S3 controller is an ESP-IDF project: build it with PlatformIO, or run ESP-IDF's export.sh first (IDF_PATH isn't set).")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pumptron_esp32s3)
