# os/freertos-esp32: DelegateMQ on an ESP32 under ESP-IDF's FreeRTOS, run
# under Espressif's QEMU. An ESP-IDF project: see README.md.
#
#   . $IDF_PATH/export.sh
#   idf.py build
#   idf.py qemu

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "os/freertos-esp32 is an ESP-IDF project: run ESP-IDF's export.sh first (IDF_PATH isn't set).")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(delegate_app)
