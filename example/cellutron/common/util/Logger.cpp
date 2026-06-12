#include "Logger.h"

#ifdef DMQ_THREAD_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static SemaphoreHandle_t xStdioMutex = NULL;
static StaticSemaphore_t xMutexBuffer;

extern "C" void Logger_Init(void) {
    if (xStdioMutex == NULL) {
        xStdioMutex = xSemaphoreCreateRecursiveMutexStatic(&xMutexBuffer);
    }
}

extern "C" void dmq_safe_printf(const char* format, ...) {
    // Check if scheduler is running or mutex not yet created. 
    // If so, just call vprintf.
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || xStdioMutex == NULL) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        return;
    }

    if (xSemaphoreTakeRecursive(xStdioMutex, portMAX_DELAY) == pdTRUE) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        xSemaphoreGiveRecursive(xStdioMutex);
    }
}
#endif
