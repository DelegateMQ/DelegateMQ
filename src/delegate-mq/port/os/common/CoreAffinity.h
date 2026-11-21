#ifndef DMQ_CORE_AFFINITY_H
#define DMQ_CORE_AFFINITY_H

/// @file CoreAffinity.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Core affinity for the desktop thread ports (stdlib, Win32, POSIX):
/// pin the calling thread to a set of cores, and ask which core it runs on.
///
/// A thread port calls SetCurrentThreadCoreAffinity() from the new thread
/// itself, before its start handler, with the mask set by SetCoreAffinity().
/// The FreeRTOS port picks the core when it creates the task instead
/// (FreeRTOSThread.cpp).
///
/// Core masks: bit 0 = core 0, bit 1 = core 1, ... 0 = any core (no change).

#include <cstdint>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
#endif

namespace dmq::os::detail {

/// Pin the calling thread to the cores in coreMask (0: any core, no change).
/// @return false if the OS refused (e.g. a core that doesn't exist), or the
/// platform has no thread affinity (macOS) and coreMask isn't 0.
inline bool SetCurrentThreadCoreAffinity(uint32_t coreMask)
{
    if (coreMask == 0)
        return true;
#if defined(_WIN32)
    return ::SetThreadAffinityMask(::GetCurrentThread(), static_cast<DWORD_PTR>(coreMask)) != 0;
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int core = 0; core < 32; core++) {
        if (coreMask & (1u << core))
            CPU_SET(core, &set);
    }
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    return false;
#endif
}

/// The core running the calling thread, or -1 if the platform can't tell
inline int GetCurrentCore()
{
#if defined(_WIN32)
    return static_cast<int>(::GetCurrentProcessorNumber());
#elif defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

} // namespace dmq::os::detail

#endif // DMQ_CORE_AFFINITY_H
