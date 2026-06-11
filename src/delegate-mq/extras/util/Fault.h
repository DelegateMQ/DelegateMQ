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
namespace dmq::util {
	/// Handles all software assertions in the system.
	/// @param[in] file - the file name that the software assertion occurred on
	/// @param[in] line - the line number that the software assertion occurred on
	DMQ_NORETURN void FaultHandler(const char* file, unsigned short line);

    /// Install OS-specific crash handlers to capture and report critical errors.
    void InstallCrashHandlers();
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
#define ASSERT() \
	dmq::util::FaultHandler(__FILE__, static_cast<unsigned short>(__LINE__))

#define ASSERT_TRUE(condition) \
	do {if (!(condition)) dmq::util::FaultHandler(__FILE__, static_cast<unsigned short>(__LINE__));} while (0)
#else
#define ASSERT() \
	FaultHandler(__FILE__, (unsigned short) __LINE__)

#define ASSERT_TRUE(condition) \
	do {if (!(condition)) FaultHandler(__FILE__, (unsigned short) __LINE__);} while (0)
#endif

#endif 
