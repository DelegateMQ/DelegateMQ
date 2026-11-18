/// @file not_linked.cpp
/// @brief Built in a target that doesn't link DelegateMQ::DelegateMQ, to check
/// that adding DelegateMQ leaves other targets' compile definitions alone
/// (docs/BUILD.md, "With CMake"). The DMQ_* options belong to the DelegateMQ
/// target and the targets that link it.

#if defined(DMQ_ALLOCATOR) || defined(DMQ_THREAD_STDLIB) || defined(DMQ_SERIALIZE_SERIALIZE)
#error "DelegateMQ's DMQ_* definitions reached a target that doesn't link DelegateMQ::DelegateMQ"
#endif

int NotLinkedValue() { return 42; }
