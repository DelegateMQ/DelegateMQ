#ifndef _LOGGER_H
#define _LOGGER_H

#include <cstdio>
#include <cstdarg>

// Only use safe printf if FreeRTOS is the thread port
#ifdef DMQ_THREAD_FREERTOS

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Initialize the thread-safe printf mutex. 
/// Must be called before the scheduler starts.
void Logger_Init(void);

/// @brief Thread-safe printf for FreeRTOS.
void dmq_safe_printf(const char* format, ...);

#ifdef __cplusplus
}
#endif

// Redefine printf to use our safe version
#undef printf
#define printf dmq_safe_printf

#endif // DMQ_THREAD_FREERTOS

#endif // _LOGGER_H
