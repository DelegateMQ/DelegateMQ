#ifndef _FAULT_H
#define _FAULT_H

#ifdef __cplusplus
    #define DMQ_NORETURN [[noreturn]]
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define DMQ_NORETURN _Noreturn
#elif defined(__GNUC__) || defined(__clang__)
    #define DMQ_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
    #define DMQ_NORETURN __declspec(noreturn)
#else
    #define DMQ_NORETURN
#endif

#ifdef __cplusplus
#include <atomic>

namespace dmq::util {
	/// Handles all software assertions in the system.
	/// @param[in] file - the file name that the software assertion occurred on
	/// @param[in] line - the line number that the software assertion occurred on
	DMQ_NORETURN void FaultHandler(const char* file, unsigned short line);

    /// Install OS-specific crash handlers to capture and report critical errors.
    void InstallCrashHandlers();

    /// @brief A fault hook: an application function the default FaultHandler()
    /// calls (after printing) before it halts, to capture the failure: write a log
    /// to RAM that survives the reset or to flash, start a core dump. DelegateMQ
    /// captures nothing itself. A plain function, not a delegate: the system is
    /// failing, so the hook must not allocate, lock, or use delegates or the
    /// DataBus, and shouldn't try to recover. A fault inside the hook doesn't call
    /// it again. For a watchdog expiry, see dmq::os::SetWatchdogHook().
    using FaultHook = void (*)(const char* file, unsigned short line);

    namespace detail {
        inline std::atomic<FaultHook> g_faultHook{nullptr};
    }

    /// @brief Set the fault hook; nullptr removes it.
    inline void SetFaultHook(FaultHook hook) noexcept { detail::g_faultHook.store(hook); }

    /// @brief Call the fault hook, if one is set and it isn't already running.
    /// The default FaultHandler() calls it; a custom one (DMQ_CUSTOM_FAULT_HANDLER)
    /// may call it too.
    inline void RunFaultHook(const char* file, unsigned short line) noexcept
    {
        // A fault inside the hook doesn't call it again. Plain loads and stores
        // (no exchange): no libatomic call on cores without atomic instructions.
        static std::atomic<bool> running{false};
        if (running.load())
            return;
        if (FaultHook hook = detail::g_faultHook.load())
        {
            running.store(true);
            hook(file, line);
            running.store(false);
        }
    }
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
	DMQ_NORETURN void FaultHandler(const char* file, unsigned short line);
	DMQ_NORETURN void WatchdogHandler(const char* threadName);
    void InstallCrashHandlers();
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#ifndef DMQ_ASSERT
#define DMQ_ASSERT() \
	dmq::util::FaultHandler(__FILE__, static_cast<unsigned short>(__LINE__))
#endif

#ifndef DMQ_ASSERT_TRUE
#define DMQ_ASSERT_TRUE(condition) \
	do {if (!(condition)) dmq::util::FaultHandler(__FILE__, static_cast<unsigned short>(__LINE__));} while (0)
#endif
#else
#ifndef DMQ_ASSERT
#define DMQ_ASSERT() \
	FaultHandler(__FILE__, (unsigned short) __LINE__)
#endif

#ifndef DMQ_ASSERT_TRUE
#define DMQ_ASSERT_TRUE(condition) \
	do {if (!(condition)) FaultHandler(__FILE__, (unsigned short) __LINE__);} while (0)
#endif
#endif

#endif 
