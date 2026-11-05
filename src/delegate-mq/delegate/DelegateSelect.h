#ifndef _DELEGATE_SELECT_H
#define _DELEGATE_SELECT_H

/// @file
/// @brief Option selection: user config, port auto-detection and derived
/// DMQ_* macros.
///
/// Preprocessor definitions only: no code, no includes beyond the user and
/// default config headers. Library .cpp files include this (not DelegateOpt.h)
/// to decide whether they compile to anything, so an unselected file adds
/// nothing to the build. DelegateOpt.h includes it first.

// Pull in user config if provided, otherwise use library defaults.
// To provide your own config: -DDMQ_USER_CONFIG="path/to/DelegateMQConfig.h",
// or put a file named DelegateMQConfig.h on the include path and it is picked
// up automatically (define DMQ_NO_USER_CONFIG to ignore it). The config can
// select ports and options (DMQ_THREAD_*, DMQ_ALLOCATOR, ...) as well as tune
// constants, so a build without CMake needs no -D flags at all.
#ifdef DMQ_USER_CONFIG
    #include DMQ_USER_CONFIG
#elif !defined(DMQ_NO_USER_CONFIG) && defined(__has_include)
    #if __has_include("DelegateMQConfig.h")
        #include "DelegateMQConfig.h"
    #endif
#endif
#include "DelegateMQConfig_Default.h"

// --- PLATFORM AUTO-DETECTION ---
// If no threading model is defined, attempt to auto-select a default
#if !defined(DMQ_THREAD_STDLIB) && !defined(DMQ_THREAD_WIN32) && !defined(DMQ_THREAD_POSIX) && \
    !defined(DMQ_THREAD_FREERTOS) && !defined(DMQ_THREAD_THREADX) && \
    !defined(DMQ_THREAD_ZEPHYR) && !defined(DMQ_THREAD_CMSIS_RTOS2) && \
    !defined(DMQ_THREAD_NUTTX) && \
    !defined(DMQ_THREAD_QT) && !defined(DMQ_THREAD_NONE)

    #if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        #define DMQ_THREAD_STDLIB
    #else
        #define DMQ_THREAD_NONE
    #endif
#endif

// --- EMBEDDED RTOS THREAD CLASSIFICATION ---
// Named composite macros for two related but distinct "is this DMQ_THREAD_*
// an embedded RTOS port" questions, so downstream code (this file's own
// DataBus default below, extras/util/NetworkConnect.h's host-header default)
// references a name instead of each re-deriving its own copy of the
// DMQ_THREAD_* list. Adding a new DMQ_THREAD_* port means updating these two
// macros -- and Defaults.cmake's DMQ_DATABUS default, which mirrors
// DMQ_THREAD_IS_EMBEDDED_RTOS's membership and says so in its own comment --
// not every call site that happens to care about the answer.

// True for every embedded RTOS thread port. Used to decide whether desktop-
// oriented defaults (DataBus auto-enable, below) should apply.
#if defined(DMQ_THREAD_FREERTOS) || defined(DMQ_THREAD_THREADX) || \
    defined(DMQ_THREAD_ZEPHYR) || defined(DMQ_THREAD_CMSIS_RTOS2) || \
    defined(DMQ_THREAD_NUTTX)
    #define DMQ_THREAD_IS_EMBEDDED_RTOS
#endif

// True only for the subset of embedded RTOS thread ports whose simulator/
// native build bundles its own network stack headers that collide with real
// host BSD/Winsock headers (e.g. Zephyr's <zephyr/net/socket.h> redefining
// sockaddr_in et al.). FreeRTOS is deliberately NOT included here: its
// Win32/POSIX simulator ports (databus-freertos, freertos-linux) have no
// native network stack of their own to collide with, and use real host
// sockets on purpose.
#if defined(DMQ_THREAD_THREADX) || defined(DMQ_THREAD_ZEPHYR) || defined(DMQ_THREAD_CMSIS_RTOS2)
    #define DMQ_THREAD_HAS_NATIVE_NETWORK_STACK
#endif

// True when a real thread model is configured (desktop or embedded RTOS),
// as opposed to DMQ_THREAD_NONE or no thread model at all (bare metal,
// single-threaded). Named once here instead of hand-copying this same
// 8-macro list at every call site that needs to know whether Mutex/
// ConditionVariable/std::thread-equivalent support exists.
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT) || \
    defined(DMQ_THREAD_POSIX) || \
    defined(DMQ_THREAD_FREERTOS) || defined(DMQ_THREAD_THREADX) || \
    defined(DMQ_THREAD_ZEPHYR) || defined(DMQ_THREAD_CMSIS_RTOS2) || \
    defined(DMQ_THREAD_NUTTX)
    #define DMQ_HAS_THREADING
#endif

// dmq::os::StartGate / Thread::SetStartGate() (port/os/common/StartGate.h):
// every thread port. Code also built for bare metal tests this first.
#if defined(DMQ_HAS_THREADING)
    #define DMQ_HAS_START_GATE
#endif

// If no serialization model is defined, attempt to auto-select a default
#if !defined(DMQ_SERIALIZE_SERIALIZE) && !defined(DMQ_SERIALIZE_RAPIDJSON) && \
    !defined(DMQ_SERIALIZE_MSGPACK) && !defined(DMQ_SERIALIZE_CEREAL) && \
    !defined(DMQ_SERIALIZE_BITSERY) && !defined(DMQ_SERIALIZE_NONE)

    #if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        #define DMQ_SERIALIZE_SERIALIZE
    #else
        #define DMQ_SERIALIZE_NONE
    #endif
#endif

// If no transport is defined, auto-select one. Matches Defaults.cmake on
// desktop (CMake always passes a transport, so this only applies to builds
// without it). RTOS and bare-metal ports default to no transport, even when
// built as a host simulator, so host socket headers aren't pulled in.
#if !defined(DMQ_TRANSPORT_NONE) && !defined(DMQ_TRANSPORT_ZEROMQ) && \
    !defined(DMQ_TRANSPORT_NNG) && !defined(DMQ_TRANSPORT_MQTT) && \
    !defined(DMQ_TRANSPORT_WIN32_PIPE) && !defined(DMQ_TRANSPORT_WIN32_UDP) && \
    !defined(DMQ_TRANSPORT_WIN32_TCP) && !defined(DMQ_TRANSPORT_LINUX_UDP) && \
    !defined(DMQ_TRANSPORT_LINUX_UDS) && !defined(DMQ_TRANSPORT_LINUX_TCP) && \
    !defined(DMQ_TRANSPORT_SERIAL_PORT) && !defined(DMQ_TRANSPORT_ARM_LWIP_UDP) && \
    !defined(DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP) && !defined(DMQ_TRANSPORT_THREADX_UDP) && \
    !defined(DMQ_TRANSPORT_STM32_UART) && !defined(DMQ_TRANSPORT_ZEPHYR_UDP)

    #if defined(DMQ_THREAD_IS_EMBEDDED_RTOS) || !defined(DMQ_HAS_THREADING)
        #define DMQ_TRANSPORT_NONE
    #elif defined(_WIN32)
        #define DMQ_TRANSPORT_WIN32_UDP
    #elif defined(__linux__)
        #define DMQ_TRANSPORT_LINUX_UDP
    #else
        #define DMQ_TRANSPORT_NONE
    #endif
#endif

// --- ONE OF EACH ---
// Two ports of a kind means two places picked one, typically CMake's DMQ_THREAD
// (passed with -D) and a #define in DelegateMQConfig.h. CMake then compiles one
// port's sources while the headers use another, so fail here instead.
#if (defined(DMQ_THREAD_STDLIB) + defined(DMQ_THREAD_WIN32) + defined(DMQ_THREAD_POSIX) + \
     defined(DMQ_THREAD_FREERTOS) + defined(DMQ_THREAD_THREADX) + defined(DMQ_THREAD_ZEPHYR) + \
     defined(DMQ_THREAD_CMSIS_RTOS2) + defined(DMQ_THREAD_NUTTX) + defined(DMQ_THREAD_QT) + \
     defined(DMQ_THREAD_NONE)) > 1
    #error "More than one DMQ_THREAD_* is defined. With CMake, set DMQ_THREAD in CMake only, not in DelegateMQConfig.h."
#endif
#if (defined(DMQ_SERIALIZE_SERIALIZE) + defined(DMQ_SERIALIZE_RAPIDJSON) + \
     defined(DMQ_SERIALIZE_MSGPACK) + defined(DMQ_SERIALIZE_CEREAL) + \
     defined(DMQ_SERIALIZE_BITSERY) + defined(DMQ_SERIALIZE_NONE)) > 1
    #error "More than one DMQ_SERIALIZE_* is defined. With CMake, set DMQ_SERIALIZE in CMake only, not in DelegateMQConfig.h."
#endif
#if (defined(DMQ_TRANSPORT_NONE) + defined(DMQ_TRANSPORT_ZEROMQ) + defined(DMQ_TRANSPORT_NNG) + \
     defined(DMQ_TRANSPORT_MQTT) + defined(DMQ_TRANSPORT_WIN32_PIPE) + defined(DMQ_TRANSPORT_WIN32_UDP) + \
     defined(DMQ_TRANSPORT_WIN32_TCP) + defined(DMQ_TRANSPORT_LINUX_UDP) + defined(DMQ_TRANSPORT_LINUX_UDS) + \
     defined(DMQ_TRANSPORT_LINUX_TCP) + defined(DMQ_TRANSPORT_SERIAL_PORT) + \
     defined(DMQ_TRANSPORT_ARM_LWIP_UDP) + defined(DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP) + \
     defined(DMQ_TRANSPORT_THREADX_UDP) + defined(DMQ_TRANSPORT_STM32_UART) + \
     defined(DMQ_TRANSPORT_ZEPHYR_UDP)) > 1
    #error "More than one DMQ_TRANSPORT_* is defined. With CMake, set DMQ_TRANSPORT in CMake only, not in DelegateMQConfig.h."
#endif

// Default to DataBus ON on Desktop if not explicitly disabled.
//
// _WIN32/__linux__/__APPLE__/__unix__ reflect the compiler/host doing the
// building, not the actual target: an RTOS simulator sample (e.g.
// zephyr-linux/threadx-linux/freertos-linux) compiles with a host GCC on a
// Unix box despite targeting DMQ_THREAD_ZEPHYR/THREADX/FREERTOS, so this
// used to auto-enable DataBus for those too even without CMake in the
// picture. DataBus's NetworkConnect.h reaches for the host's own BSD
// socket headers unconditionally on Linux/macOS/Windows, which can conflict
// outright with a target's own native network stack headers (e.g. Zephyr's
// <zephyr/net/socket.h> redefining sockaddr_in et al.) if the application
// also pulls those in directly (a UDP transport sample, for instance).
// DMQ_THREAD_IS_EMBEDDED_RTOS mirrors Defaults.cmake's equivalent CMake-
// level default, for projects that set these macros by hand instead of
// going through DelegateMQ.cmake. DMQ_THREAD_NONE is excluded as well, as in
// Defaults.cmake: DataBus's ThreadMonitor needs a thread port.
#if !defined(DMQ_DATABUS) && !defined(DMQ_DATABUS_OFF) && \
    !defined(DMQ_THREAD_IS_EMBEDDED_RTOS) && defined(DMQ_HAS_THREADING)
    #if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        #define DMQ_DATABUS
    #endif
#endif

// Default to DataBus Tools ON on Desktop if DataBus is active and not explicitly disabled
#if defined(DMQ_DATABUS) && !defined(DMQ_DATABUS_TOOLS) && !defined(DMQ_DATABUS_TOOLS_OFF)
    #if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        #define DMQ_DATABUS_TOOLS
    #endif
#endif

#endif // _DELEGATE_SELECT_H
