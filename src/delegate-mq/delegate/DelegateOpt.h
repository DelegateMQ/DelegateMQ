#ifndef _DELEGATE_OPT_H
#define _DELEGATE_OPT_H

/// @file
/// @brief Delegate library options header file.

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    // If networking is active, include winsock2.h here to prevent macro 
    // redefinitions when windows.h is included later (e.g. by FreeRTOS or stdlib).
    #if defined(DMQ_DATABUS) || defined(DMQ_TRANSPORT_ZEROMQ) || \
        defined(DMQ_TRANSPORT_WIN32_UDP) || defined(DMQ_TRANSPORT_WIN32_TCP) || \
        defined(DMQ_TRANSPORT_WIN32_PIPE) || defined(DMQ_TRANSPORT_NNG) || \
        defined(DMQ_TRANSPORT_MQTT)
        #include <winsock2.h>
        #include <ws2tcpip.h>
    #endif
#endif

// --- PLATFORM AUTO-DETECTION ---
// If no threading model is defined, attempt to auto-select a default
#if !defined(DMQ_THREAD_STDLIB) && !defined(DMQ_THREAD_WIN32) && \
    !defined(DMQ_THREAD_FREERTOS) && !defined(DMQ_THREAD_THREADX) && \
    !defined(DMQ_THREAD_ZEPHYR) && !defined(DMQ_THREAD_CMSIS_RTOS2) && \
    !defined(DMQ_THREAD_QT) && !defined(DMQ_THREAD_NONE)

    #if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        #define DMQ_THREAD_STDLIB
    #endif
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

// Default to DataBus ON on Desktop if not explicitly disabled
#if !defined(DMQ_DATABUS) && !defined(DMQ_DATABUS_OFF)
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

#include <chrono>
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT)
    #include <mutex>
#endif

// RTTI Detection Check
#if !defined(__cpp_rtti) && !defined(__GXX_RTTI) && !defined(_CPPRTTI)
    #error "RTTI compiler option is disabled but required by the DelegateMQ library."
#endif

#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT)
    // Windows / Linux / macOS / Qt (Standard Library)
    #include <condition_variable>
#elif defined(DMQ_THREAD_FREERTOS)
    #include <mutex>
    #include "port/os/freertos/FreeRTOSClock.h"
    #include "port/os/freertos/FreeRTOSMutex.h"
    #include "port/os/freertos/FreeRTOSConditionVariable.h"
#elif defined(DMQ_THREAD_THREADX)
    #include <mutex>
    #include "port/os/threadx/ThreadXClock.h"
    #include "port/os/threadx/ThreadXMutex.h"
    #include "port/os/threadx/ThreadXConditionVariable.h"
#elif defined(DMQ_THREAD_ZEPHYR)
    #include "port/os/zephyr/ZephyrClock.h"
    #include "port/os/zephyr/ZephyrMutex.h"
#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    #include "port/os/cmsis-rtos2/CmsisRtos2Clock.h"
    #include "port/os/cmsis-rtos2/CmsisRtos2Mutex.h"
#elif defined(DMQ_THREAD_NONE)
    #include "port/os/bare-metal/BareMetalClock.h"
#else
    #include "port/os/bare-metal/BareMetalClock.h"
#endif

namespace dmq
{
    // --- PORTABLE LOCK GUARD ---
    // Does not require <mutex>. Works with any BasicLockable type (lock/unlock).
    template<typename T>
    class PortableLockGuard {
        T& m_mutex;
    public:
        explicit PortableLockGuard(T& m) noexcept : m_mutex(m) { m_mutex.lock(); }
        ~PortableLockGuard() noexcept { m_mutex.unlock(); }
        PortableLockGuard(const PortableLockGuard&) = delete;
        PortableLockGuard& operator=(const PortableLockGuard&) = delete;
    };

    // @TODO: Change aliases to switch clock type globally if necessary

    // --- CLOCK SELECTION ---
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT)
    // Windows / Linux / macOS / Qt
    using Clock = std::chrono::steady_clock;

#elif defined(DMQ_THREAD_FREERTOS)
    // Use the custom FreeRTOS wrapper
    using Clock = dmq::os::FreeRTOSClock;

#elif defined(DMQ_THREAD_THREADX)
    // Use the custom ThreadX wrapper
    using Clock = dmq::os::ThreadXClock;

#elif defined(DMQ_THREAD_ZEPHYR)
    // Use the custom Zephyr wrapper
    using Clock = dmq::os::ZephyrClock;

#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    using Clock = dmq::os::CmsisRtos2Clock;

#else
    // Assuming implemented the 'g_ticks' variable
    using Clock = dmq::os::BareMetalClock;
#endif

    // --- GENERIC TYPES ---
    // Automatically adapt to the underlying Clock's traits
    using Duration = typename Clock::duration;
    using TimePoint = typename Clock::time_point;

    /// @brief Default timeout for the TIMEOUT queue-full policy across all Thread ports.
    /// Override per-thread at construction or project-wide before including this header.
    inline constexpr std::chrono::seconds DEFAULT_DISPATCH_TIMEOUT{2};

    // --- MUTEX / LOCK SELECTION ---
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT)
    // Windows / Linux / macOS / Qt
    using Mutex = std::mutex;
    using RecursiveMutex = std::recursive_mutex;
    using ConditionVariable = std::condition_variable;
    template<typename T> using LockGuard = std::lock_guard<T>;
    template<typename T> using UniqueLock = std::unique_lock<T>;
    #define DMQ_HAS_CV

#elif defined(DMQ_THREAD_FREERTOS)
    // Use the custom FreeRTOS wrapper
    using Mutex = dmq::os::FreeRTOSMutex;
    using RecursiveMutex = dmq::os::FreeRTOSRecursiveMutex;
    using ConditionVariable = dmq::os::FreeRTOSConditionVariable;
    template<typename T> using LockGuard = PortableLockGuard<T>;
    template<typename T> using UniqueLock = std::unique_lock<T>;
    #define DMQ_HAS_CV

#elif defined(DMQ_THREAD_THREADX)
    // Use the custom ThreadX wrapper
    using Mutex = dmq::os::ThreadXMutex;
    using RecursiveMutex = dmq::os::ThreadXRecursiveMutex;
    using ConditionVariable = dmq::os::ThreadXConditionVariable;
    template<typename T> using LockGuard = PortableLockGuard<T>;
    template<typename T> using UniqueLock = std::unique_lock<T>;
    #define DMQ_HAS_CV

#elif defined(DMQ_THREAD_ZEPHYR)
    // Use the custom Zephyr wrapper
    using Mutex = dmq::os::ZephyrMutex;
    using RecursiveMutex = dmq::os::ZephyrRecursiveMutex;
    template<typename T> using LockGuard = PortableLockGuard<T>;

#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    using Mutex = dmq::os::CmsisRtos2Mutex;
    using RecursiveMutex = dmq::os::CmsisRtos2RecursiveMutex;
    template<typename T> using LockGuard = PortableLockGuard<T>;

#else
    // Bare metal has no threads, so no locking is required.
    // NullMutex satisfies BasicLockable; PortableLockGuard compiles to nothing meaningful.
    struct NullMutex {
        void lock() {}
        void unlock() {}
    };
    using Mutex = NullMutex;
    using RecursiveMutex = NullMutex;
    template<typename T> using LockGuard = PortableLockGuard<T>;
    // No DMQ_HAS_CV — Semaphore and DelegateAsyncWait are unavailable on bare metal
#endif
}

// Detect if exceptions are disabled at the compiler level
#if !defined(__cpp_exceptions)
    #ifndef DMQ_ASSERTS
        #define DMQ_ASSERTS  // Force asserts if exceptions are off
    #endif
#endif

// @TODO: Select the desired software fault handling (see Port.cmake).
#ifdef DMQ_ASSERTS
    #include "extras/util/Fault.h"
    #include <cassert>
    // Use assert error handling. Change assert to a different error 
    // handler as required by the target application.
    #define BAD_ALLOC() assert(false && "Memory allocation failed!")
#else
    #include "extras/util/Fault.h"
    #include <new>
    // Use exception error handling
    #define BAD_ALLOC() throw std::bad_alloc()
#endif

// @TODO: Select the desired heap allocation (see Port.cmake).
// If DMQ_ASSERTS defined above, consider defining DMQ_ALLOCATOR to prevent 
// std::list usage within delegate library from throwing a std::bad_alloc 
// exception. The std_allocator calls assert if out of memory. 
// See master CMakeLists.txt for info on enabling the fixed-block allocator.
#ifdef DMQ_ALLOCATOR
    // Use stl_allocator fixed-block allocator for dynamic storage allocation
    #include "extras/allocator/xstring.h"
    #include "extras/allocator/xlist.h"
    #include "extras/allocator/xsstream.h"
    #include "extras/allocator/stl_allocator.h"
    #include "extras/allocator/xnew.h"
    #include "extras/allocator/xmake_shared.h"
#else
    #include <string>
    #include <list>
    #include <sstream>
    #include <memory>
    #include <utility>

    // Not using xallocator; define as nothing
    #undef XALLOCATOR
    #define XALLOCATOR

    namespace dmq {
        // Use default std::allocator for dynamic storage allocation
        template <typename T, typename Alloc = std::allocator<T>>
        class xlist : public std::list<T, Alloc> {
        public:
            using std::list<T, Alloc>::list; // Inherit constructors
            using std::list<T, Alloc>::operator=;
        };

        typedef std::basic_ostringstream<char, std::char_traits<char>> xostringstream;
        typedef std::basic_stringstream<char, std::char_traits<char>> xstringstream;

        typedef std::string xstring;

        // Fallback xmake_shared — uses std::make_shared when fixed-block allocator is disabled
        template <typename T, typename... Args>
        inline std::shared_ptr<T> xmake_shared(Args&&... args)
        {
            return std::make_shared<T>(std::forward<Args>(args)...);
        }

        // Fallback xnew/xdelete — use standard new/delete when fixed-block allocator is disabled
        template<typename T, typename... Args>
        inline T* xnew(Args&&... args) {
            return new(std::nothrow) T(std::forward<Args>(args)...);
        }

        template<typename T>
        inline void xdelete(T* p) {
            delete p;
        }
    }
#endif

// @TODO: Select the desired logging (see Port.cmake).
#ifdef DMQ_LOG
    #include <spdlog/spdlog.h>
    #define LOG_INFO(...)    spdlog::info(__VA_ARGS__)
    #define LOG_DEBUG(...)   spdlog::debug(__VA_ARGS__)
    #define LOG_ERROR(...)   spdlog::error(__VA_ARGS__)
#else
    // No-op macros when logging disabled
    #define LOG_INFO(...)    do {} while(0)
    #define LOG_DEBUG(...)   do {} while(0)
    #define LOG_ERROR(...)   do {} while(0)
#endif

#endif // _DELEGATE_OPT_H