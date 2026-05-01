#ifndef _DELEGATE_MQ_H
#define _DELEGATE_MQ_H

// Delegate.h
// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, 2025.

/// @file DelegateMQ.h
/// @brief A single-include header for the complete DelegateMQ library functionality.
///
/// @details
/// DelegateMQ is a robust C++ delegate library that enables invoking any callable function 
/// (synchronously or asynchronously) on a specific user-defined thread of control. It also 
/// supports remote function invocation over any transport protocol.
///
/// **Key Features:**
/// * **Universal Target Support:** Binds to free functions, class member functions, static functions, 
///   lambdas, and `std::function`.
/// * **Any Signature:** Handles any function signature with any number of arguments or return values.
/// * **Argument Safety:** Supports all argument types (value, pointer, pointer-to-pointer, reference) 
///   and safely marshals them across thread boundaries for asynchronous calls.
/// * **Thread Control:** Unlike `std::async` (which uses a random thread pool), DelegateMQ executes 
///   the target function on a *specific* destination thread you control.
/// * **Remote Invocation:** Capable of serializing arguments and invoking functions across network 
///   boundaries (UDP, TCP, ZeroMQ, etc.).
///
/// **Delegate Capabilities:**
/// A delegate instance behaves like a first-class object:
/// * **Copyable:** Can be copied freely.
/// * **Comparable:** Supports equality checks against other delegates or `nullptr`.
/// * **Assignable:** Can be reassigned at runtime.
/// * **Callable:** Invoked via `operator()`.
///
/// **Common Use Cases:**
/// * Asynchronous Method Invocation (AMI) on specific worker threads.
/// * Publish / Subscribe (Observer) patterns.
/// * Anonymous, thread-safe asynchronous callbacks.
/// * Event-Driven Programming architectures.
/// * Thread-Safe Asynchronous APIs.
/// * Active Object design patterns.
///
/// **Asynchronous Safety:**
/// Asynchronous variants automatically copy argument data into the event queue. This provides true 
/// 'fire and forget' functionality, ensuring that out-of-scope stack variables in the caller 
/// do not cause data races or corruption in the target thread.
///
/// **Error Handling:**
/// The `Async` and `AsyncWait` variants may throw `std::bad_alloc` if heap allocation fails during 
/// invocation. Alternatively, defining `DMQ_ASSERTS` switches error handling to assertions. 
/// All other delegate functions are `noexcept`.
///
/// **Documentation & Source:**
/// * Repository: https://github.com/DelegateMQ/DelegateMQ
/// * See `README.md`, `DETAILS.md`, and `EXAMPLES.md` for comprehensive guides.

// -----------------------------------------------------------------------------
// 0. Platform Configuration
// -----------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

// -----------------------------------------------------------------------------
// 1. Core Non-Thread-Safe Delegates
// (Always available: Bare Metal, FreeRTOS, Windows, Linux)
// -----------------------------------------------------------------------------
#include "delegate/Delegate.h"
#include "delegate/DelegateRemote.h"
#include "delegate/MulticastDelegate.h"
#include "delegate/UnicastDelegate.h"
#include "delegate/Signal.h"

// -----------------------------------------------------------------------------
// 2. Thread-Safe Wrappers (Mutex Only)
// -----------------------------------------------------------------------------
// - FreeRTOS: Uses FreeRTOSRecursiveMutex
// - Bare Metal: Uses NullMutex
// - StdLib: Uses std::recursive_mutex
// Valid for any platform where a Mutex is defined in DelegateOpt.h
#if defined(DMQ_THREAD_STDLIB) || \
    defined(DMQ_THREAD_WIN32) || \
    defined(DMQ_THREAD_FREERTOS) || \
    defined(DMQ_THREAD_THREADX) || \
    defined(DMQ_THREAD_ZEPHYR) || \
    defined(DMQ_THREAD_CMSIS_RTOS2) || \
    defined(DMQ_THREAD_QT) || \
    defined(DMQ_THREAD_NONE)
    #include "delegate/MulticastDelegateSafe.h"
    #include "delegate/UnicastDelegateSafe.h"
#endif

// -----------------------------------------------------------------------------
// 3. Asynchronous "Fire and Forget" Delegates
// -----------------------------------------------------------------------------
// - FreeRTOS: OK 
// - Bare Metal: OK (Requires you to implement IThread wrapper for Event Loop)
// - StdLib / Win32: OK
// Valid for any platform that implements the IThread interface
#if defined(DMQ_THREAD_STDLIB) || \
    defined(DMQ_THREAD_WIN32) || \
    defined(DMQ_THREAD_FREERTOS) || \
    defined(DMQ_THREAD_THREADX) || \
    defined(DMQ_THREAD_ZEPHYR) || \
    defined(DMQ_THREAD_CMSIS_RTOS2) || \
    defined(DMQ_THREAD_QT)
    #include "delegate/DelegateAsync.h"
#endif

// -----------------------------------------------------------------------------
// 4. Asynchronous "Blocking" Delegates (Wait for Result)
// -----------------------------------------------------------------------------
// Depends on Semaphore/Mutex and C++17 (std::any, std::optional).
// Valid for StdLib/Win32 (Windows/Linux), Qt, ThreadX, and FreeRTOS (if C++17 enabled).
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT) || defined(DMQ_THREAD_FREERTOS) || defined(DMQ_THREAD_THREADX)
    #include "delegate/DelegateAsyncWait.h"
#endif

#if defined(DMQ_THREAD_STDLIB)
    #include "port/os/stdlib/Thread.h"
    #include "port/os/stdlib/ThreadMsg.h"
#elif defined(DMQ_THREAD_WIN32)
    #include "port/os/win32/Thread.h"
    #include "port/os/win32/ThreadMsg.h"
#elif defined(DMQ_THREAD_FREERTOS)
    #include "port/os/freertos/Thread.h"
    #include "port/os/freertos/ThreadMsg.h"
#elif defined(DMQ_THREAD_THREADX)
    #include "port/os/threadx/Thread.h"
    #include "port/os/threadx/ThreadMsg.h"
#elif defined(DMQ_THREAD_ZEPHYR)
    #include "port/os/zephyr/Thread.h"
    #include "port/os/zephyr/ThreadMsg.h"
#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    #include "port/os/cmsis-rtos2/Thread.h"
    #include "port/os/cmsis-rtos2/ThreadMsg.h"
#elif defined(DMQ_THREAD_QT)
    #include "port/os/qt/Thread.h"
    #include "port/os/qt/ThreadMsg.h"
#elif defined(DMQ_THREAD_NONE)
    // Bare metal: User must implement their own polling/interrupt logic
#else
    #warning "Thread implementation not found."
    #define DMQ_THREAD_NONE
#endif

#if defined(DMQ_SERIALIZE_MSGPACK)
    #include "port/serialize/msgpack/Serializer.h"
#elif defined(DMQ_SERIALIZE_CEREAL)
    #include "port/serialize/cereal/Serializer.h"
#elif defined(DMQ_SERIALIZE_BITSERY)
    #include "port/serialize/bitsery/Serializer.h"
#elif defined(DMQ_SERIALIZE_RAPIDJSON)
    #include "port/serialize/rapidjson/Serializer.h"
#elif defined(DMQ_SERIALIZE_SERIALIZE)
    #include "port/serialize/serialize/Serializer.h"
#elif defined(DMQ_SERIALIZE_NONE)
    // Create a custom application-specific serializer
#else
    #warning "Serialize implementation not found."
#endif

#if defined(DMQ_TRANSPORT_ZEROMQ)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/zeromq/ZeroMqTransport.h"
#elif defined(DMQ_TRANSPORT_NNG)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/nng/NngTransport.h"
#elif defined(DMQ_TRANSPORT_WIN32_PIPE)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/win32-pipe/Win32PipeTransport.h"
#elif defined(DMQ_TRANSPORT_WIN32_UDP)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/win32-udp/Win32UdpTransport.h"
    #include "port/transport/win32-udp/MulticastTransport.h"
#elif defined(DMQ_TRANSPORT_WIN32_TCP)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/win32-tcp/Win32TcpTransport.h"
#elif defined(DMQ_TRANSPORT_LINUX_UDP)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/linux-udp/LinuxUdpTransport.h"
    #include "port/transport/linux-udp/MulticastTransport.h"
#elif defined(DMQ_TRANSPORT_LINUX_TCP)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/linux-tcp/LinuxTcpTransport.h"
#elif defined(DMQ_TRANSPORT_MQTT)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/mqtt/MqttTransport.h"
#elif defined(DMQ_TRANSPORT_SERIAL_PORT)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/serial/SerialTransport.h"
#elif defined(DMQ_TRANSPORT_ARM_LWIP_UDP)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/arm-lwip-udp/ArmLwipUdpTransport.h"
#elif defined(DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/arm-lwip-netconn-udp/ArmLwipNetconnUdpTransport.h"
#elif defined(DMQ_TRANSPORT_THREADX_UDP)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/threadx-udp/NetXUdpTransport.h"
#elif defined(DMQ_TRANSPORT_STM32_UART)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/stm32-uart/Stm32UartTransport.h"
#elif defined(DMQ_TRANSPORT_ZEPHYR_UDP)
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/zephyr-udp/ZephyrUdpTransport.h"
#elif defined(DMQ_TRANSPORT_NONE)
    // No built-in transport. Include the interface and dispatcher so application code
    // can implement a custom ITransport and use RemoteChannel with a mock or stub.
    #include "extras/dispatcher/Dispatcher.h"
    #include "port/transport/ITransport.h"
#else
    #warning "Transport implementation not found."
#endif

// Include RemoteChannel whenever Dispatcher.h has been included (all transport
// configurations including NONE, where a mock ITransport can be supplied).
// RemoteChannel aggregates dispatcher, serializer, and stream into one object,
// mirroring how Thread aggregates async delegate wiring behind a single IThread.
#if defined(DMQ_TRANSPORT_ZEROMQ) || defined(DMQ_TRANSPORT_NNG) || \
    defined(DMQ_TRANSPORT_WIN32_PIPE) || defined(DMQ_TRANSPORT_WIN32_UDP) || \
    defined(DMQ_TRANSPORT_WIN32_TCP) || defined(DMQ_TRANSPORT_LINUX_UDP) || \
    defined(DMQ_TRANSPORT_LINUX_TCP) || defined(DMQ_TRANSPORT_MQTT) || \
    defined(DMQ_TRANSPORT_SERIAL_PORT) || defined(DMQ_TRANSPORT_ARM_LWIP_UDP) || \
    defined(DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP) || defined(DMQ_TRANSPORT_THREADX_UDP) || \
    defined(DMQ_TRANSPORT_STM32_UART) || defined(DMQ_TRANSPORT_ZEPHYR_UDP) || \
    defined(DMQ_TRANSPORT_NONE)
    #include "extras/dispatcher/RemoteChannel.h"
#endif

#include "extras/util/Fault.h"
#include "extras/util/ClockHelper.h"

// Only include Timer and AsyncInvoke if threads exist
#if !defined(DMQ_THREAD_NONE)
    #include "extras/util/Timer.h"
    #include "extras/util/AsyncInvoke.h"
    #include "extras/util/TransportMonitor.h"
#endif

// Only include NetworkEngine if a transport that uses it is active
#if defined(DMQ_TRANSPORT_ZEROMQ) || defined(DMQ_TRANSPORT_WIN32_UDP) || \
    defined(DMQ_TRANSPORT_LINUX_UDP) || defined(DMQ_TRANSPORT_STM32_UART) || \
    defined(DMQ_TRANSPORT_SERIAL_PORT)
    #include "extras/util/NetworkEngine.h"
#endif

#if defined(DMQ_DATABUS)
    #include "extras/databus/DataBus.h"
    #include "extras/databus/Participant.h"
    #include "extras/databus/DeadlineSubscription.h"
#endif


#endif
