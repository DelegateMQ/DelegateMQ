/// @file
/// @brief Single source file for builds without CMake.
///
/// Add this one file to the project (Keil, IAR, Makefile, ...) and put
/// src/delegate-mq on the include path. It compiles every DelegateMQ source
/// file; each one compiles to nothing unless the options select it, so the
/// options in DelegateMQConfig.h (or -D flags) decide what is built.
///
/// Add either this file or the .cpp files under src/delegate-mq, not both:
/// compiling both defines everything twice. This file sits outside
/// src/delegate-mq so that IDEs that compile every source in a folder
/// (STM32CubeIDE, PlatformIO, ...) can take the whole src/delegate-mq folder
/// with nothing to exclude.
///
/// CMake builds don't use this file; DelegateMQ.cmake lists the selected
/// sources itself.

// OS / thread ports (one is selected by DMQ_THREAD_*)
#include "delegate-mq/port/os/stdlib/StdlibThread.cpp"
#include "delegate-mq/port/os/win32/Win32Thread.cpp"
#include "delegate-mq/port/os/posix/PosixThread.cpp"
#include "delegate-mq/port/os/freertos/FreeRTOSThread.cpp"
#include "delegate-mq/port/os/threadx/ThreadXThread.cpp"
#include "delegate-mq/port/os/zephyr/ZephyrThread.cpp"
#include "delegate-mq/port/os/cmsis-rtos2/CmsisRtos2Thread.cpp"
#include "delegate-mq/port/os/nuttx/NuttXThread.cpp"
#include "delegate-mq/port/os/qt/QtThread.cpp"

// Fixed-block allocator (DMQ_ALLOCATOR)
#include "delegate-mq/extras/allocator/Allocator.cpp"
#include "delegate-mq/extras/allocator/xallocator.cpp"

// Fault handler (DMQ_CUSTOM_FAULT_HANDLER to supply your own)
#include "delegate-mq/port/fault/Fault.cpp"

// Utilities
#include "delegate-mq/extras/util/Timer.cpp"
#include "delegate-mq/extras/util/ThreadMonitor.cpp"
#include "delegate-mq/extras/rpc/RemoteDispatcher.cpp"
