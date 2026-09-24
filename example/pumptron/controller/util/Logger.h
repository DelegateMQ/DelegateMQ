#ifndef PUMPTRON_LOGGER_H
#define PUMPTRON_LOGGER_H

/// @file Logger.h
/// @brief Serializes printf() across FreeRTOS tasks.
///
/// Included from DelegateMQConfig.h, so it applies to every controller source
/// file -- application and DelegateMQ library alike -- and redirects printf()
/// through a FreeRTOS recursive mutex.
///
/// Why: on the FreeRTOS Windows simulator every task is a Windows thread that
/// the port suspends with SuspendThread(). A task suspended inside printf()
/// holds the C runtime's stdout lock at the OS level, invisible to FreeRTOS;
/// the next task to print then blocks at the OS level while FreeRTOS still
/// considers it running, stalling it (and anything of lower priority).
/// A FreeRTOS mutex makes the ownership visible to the scheduler. On the F4 it
/// also protects newlib's non-reentrant stdio (configUSE_NEWLIB_REENTRANT 0).
/// Same approach as Cellutron's common/util/Logger.h.

#include <cstdio>
#include <cstdarg>

#ifdef DMQ_THREAD_FREERTOS

#ifdef __cplusplus
extern "C" {
#endif

/// Create the stdio mutex. Call once from main() before starting the scheduler.
void Logger_Init(void);

void pumptron_safe_printf(const char* format, ...);

#ifdef __cplusplus
}
#endif

#undef printf
#define printf pumptron_safe_printf

#endif // DMQ_THREAD_FREERTOS

#endif // PUMPTRON_LOGGER_H
