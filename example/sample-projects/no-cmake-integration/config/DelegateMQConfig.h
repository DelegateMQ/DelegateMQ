#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

/// @file
/// @brief DelegateMQ options for the no-cmake-integration sample.
///
/// Found automatically because config/ is on the include path; no -D flags.
/// Anything not defined here is auto-selected: stdlib threads, the built-in
/// serializer, and UDP on Windows/Linux. See
/// src/delegate-mq/delegate/DelegateMQConfig_Template.h for every option.

// Route library allocations through the fixed-block allocator.
#define DMQ_ALLOCATOR

// Tuning constants can be set here too.
#define DMQ_DEFAULT_DISPATCH_TIMEOUT    2

#endif // _DELEGATEMQ_CONFIG_H
