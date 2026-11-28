#ifndef DMQ_DESKTOP_PRIORITY_H
#define DMQ_DESKTOP_PRIORITY_H

/// @file DesktopPriority.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::os::ThreadPriority bands on the desktop thread ports (stdlib,
/// Win32, POSIX, Qt), as the OS's own per-thread priorities:
///
/// | Band          | Windows                         | Linux (nice) |
/// |---------------|---------------------------------|--------------|
/// | IDLE          | THREAD_PRIORITY_IDLE            | 19           |
/// | LOWEST        | THREAD_PRIORITY_LOWEST          | 10           |
/// | BELOW_NORMAL  | THREAD_PRIORITY_BELOW_NORMAL    | 5            |
/// | NORMAL        | THREAD_PRIORITY_NORMAL          | 0            |
/// | ABOVE_NORMAL  | THREAD_PRIORITY_ABOVE_NORMAL    | -5           |
/// | HIGHEST       | THREAD_PRIORITY_HIGHEST         | -10          |
/// | TIME_CRITICAL | THREAD_PRIORITY_TIME_CRITICAL   | -20          |
///
/// Linux: an unprivileged process can only lower a thread's priority (raise
/// its nice value); anything else needs CAP_SYS_NICE or RLIMIT_NICE, and the
/// call returns false. Other platforms (macOS) have no per-thread setting
/// here: false for any band but NORMAL. The desktop ports ignore a step and
/// the DMQ_PRIORITY_BAND_* config, which are for the RTOS ports.
///
/// A port keeps the thread's OS id, band and "band set" flag as atomics,
/// calls StartDesktopPriority() on the new thread before its start handler,
/// SetDesktopPriority() from SetPriority(), and clears the id at the start of
/// ExitThread(), while the thread still holds it, so a later call can't reach
/// a reused id. Included by the
/// ports' .cpp files only: it pulls in <windows.h>.

#include "ThreadPriority.h"
#include <atomic>
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
    #include <sys/resource.h>
    #include <sys/syscall.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

namespace dmq::os::detail {

/// The calling thread's OS id (never 0), or 0 where the platform has no
/// per-thread priority here.
inline uint64_t CurrentNativeThreadId()
{
#if defined(_WIN32)
    return static_cast<uint64_t>(::GetCurrentThreadId());
#elif defined(__linux__)
    return static_cast<uint64_t>(::syscall(SYS_gettid));
#else
    return 0;
#endif
}

/// true for a band within IDLE ... TIME_CRITICAL
inline bool IsPriorityBand(ThreadPriority band)
{
    const int r = static_cast<int>(band);
    return r >= 0 && r < PRIORITY_BAND_COUNT;
}

/// Set the priority of the thread with this OS id to a band.
/// @return false if the band is invalid, the OS refused, or the platform has
/// no per-thread priority here and band isn't NORMAL.
inline bool SetNativeThreadPriority(uint64_t id, ThreadPriority band)
{
    if (!IsPriorityBand(band))
        return false;
    const int r = static_cast<int>(band);
#if defined(_WIN32)
    static const int LEVELS[PRIORITY_BAND_COUNT] = {
        THREAD_PRIORITY_IDLE, THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
        THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST,
        THREAD_PRIORITY_TIME_CRITICAL };
    if (id == 0)
        return false;
    // The calling thread itself: its pseudo-handle, no OpenThread()
    if (id == CurrentNativeThreadId())
        return ::SetThreadPriority(::GetCurrentThread(), LEVELS[r]) != 0;
    HANDLE h = ::OpenThread(THREAD_SET_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(id));
    if (h == NULL)
        return false;
    const BOOL ok = ::SetThreadPriority(h, LEVELS[r]);
    ::CloseHandle(h);
    return ok != 0;
#elif defined(__linux__)
    static const int NICE[PRIORITY_BAND_COUNT] = { 19, 10, 5, 0, -5, -10, -20 };
    if (id == 0)
        return false;
    return ::setpriority(PRIO_PROCESS, static_cast<id_t>(id), NICE[r]) == 0;
#else
    (void)id;
    (void)r;
    return band == ThreadPriority::NORMAL;
#endif
}

/// From the new thread, before its start handler: record its OS id, and
/// apply a band set before CreateThread(). Best effort: a refusal leaves the
/// thread at its default priority. Applies again if a concurrent
/// SetPriority() changed the band meanwhile, so the newest band wins.
inline void StartDesktopPriority(std::atomic<uint64_t>& id,
    const std::atomic<ThreadPriority>& band, const std::atomic<bool>& bandSet)
{
    const uint64_t tid = CurrentNativeThreadId();
    id.store(tid);
    if (!bandSet.load())
        return;
    ThreadPriority applied;
    do {
        applied = band.load();
        (void)SetNativeThreadPriority(tid, applied);
    } while (band.load() != applied);
}

/// SetPriority() on a desktop port: store the band, and apply it now if the
/// thread is running (id != 0). An invalid band changes nothing.
/// @return false if the band is invalid, the OS refused it on the running
/// thread, or the platform has no per-thread priority here and newBand isn't
/// NORMAL.
/// @note Don't call it concurrently with ExitThread(): a thread that has just
/// exited may have had its OS id reused.
inline bool SetDesktopPriority(const std::atomic<uint64_t>& id,
    std::atomic<ThreadPriority>& band, std::atomic<bool>& bandSet, ThreadPriority newBand)
{
    if (!IsPriorityBand(newBand))
        return false;
    band.store(newBand);
    bandSet.store(true);
#if defined(_WIN32) || defined(__linux__)
    const uint64_t running = id.load();
    return running == 0 ? true : SetNativeThreadPriority(running, newBand);
#else
    (void)id;
    return newBand == ThreadPriority::NORMAL;
#endif
}

} // namespace dmq::os::detail

#endif // DMQ_DESKTOP_PRIORITY_H
