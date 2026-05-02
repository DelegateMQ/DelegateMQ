#ifndef _FAULT_H
#define _FAULT_H

#ifdef __cplusplus
namespace dmq::util {
	/// Handles all software assertions in the system.
	/// @param[in] file - the file name that the software assertion occurred on
	/// @param[in] line - the line number that the software assertion occurred on
	void FaultHandler(const char* file, unsigned short line);
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
	void FaultHandler(const char* file, unsigned short line);
	void WatchdogHandler(const char* threadName);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define ASSERT() \
	dmq::util::FaultHandler(__FILE__, (unsigned short) __LINE__)

#define ASSERT_TRUE(condition) \
	do {if (!(condition)) dmq::util::FaultHandler(__FILE__, (unsigned short) __LINE__);} while (0)
#else
#define ASSERT() \
	FaultHandler(__FILE__, (unsigned short) __LINE__)

#define ASSERT_TRUE(condition) \
	do {if (!(condition)) FaultHandler(__FILE__, (unsigned short) __LINE__);} while (0)
#endif

#endif 
