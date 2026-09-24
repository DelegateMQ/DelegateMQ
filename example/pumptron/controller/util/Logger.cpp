#include "Logger.h"

#ifdef DMQ_THREAD_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static SemaphoreHandle_t s_stdioMutex = nullptr;
static StaticSemaphore_t s_stdioMutexBuffer;

extern "C" void Logger_Init(void)
{
    if (s_stdioMutex == nullptr)
        s_stdioMutex = xSemaphoreCreateRecursiveMutexStatic(&s_stdioMutexBuffer);
}

extern "C" void pumptron_safe_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    // Before the scheduler runs there is only one thread of execution.
    const bool locked = s_stdioMutex != nullptr &&
                        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
    if (locked)
        xSemaphoreTakeRecursive(s_stdioMutex, portMAX_DELAY);

    vprintf(format, args);

    if (locked)
        xSemaphoreGiveRecursive(s_stdioMutex);

    va_end(args);
}
#endif
