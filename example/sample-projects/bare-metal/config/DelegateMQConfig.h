#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

/// @file
/// @brief DelegateMQ options for the Makefile build (see Makefile).
///
/// Builds without CMake select ports and options here instead of with -D
/// flags. The CMake build (CMakeLists.txt) doesn't use this file; it sets the
/// same options with DMQ_THREAD etc.

// Bare metal: no RTOS, no serializer, no transport.
#define DMQ_THREAD_NONE
#define DMQ_SERIALIZE_NONE
#define DMQ_TRANSPORT_NONE

// main.cpp defines FaultHandler()/WatchdogHandler(), so the library's
// port/fault/Fault.cpp compiles to nothing.
#define DMQ_CUSTOM_FAULT_HANDLER

#endif // _DELEGATEMQ_CONFIG_H
