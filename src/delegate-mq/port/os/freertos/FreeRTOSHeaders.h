#ifndef FREERTOS_HEADERS_H
#define FREERTOS_HEADERS_H

/// @file FreeRTOSHeaders.h
/// @brief FreeRTOS kernel headers used by the FreeRTOS port.
/// @details ESP-IDF (ESP32, including Arduino-ESP32 and PlatformIO) ships the
/// kernel headers under a freertos/ prefix; every other FreeRTOS build puts
/// the kernel include directory on the path directly.

#if defined(ESP_PLATFORM)
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    #include "freertos/queue.h"
#else
    #include "FreeRTOS.h"
    #include "task.h"
    #include "semphr.h"
    #include "queue.h"
#endif

#endif // FREERTOS_HEADERS_H
