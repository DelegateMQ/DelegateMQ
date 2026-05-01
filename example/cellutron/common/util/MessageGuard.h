#ifndef MESSAGE_GUARD_H
#define MESSAGE_GUARD_H

#include "extras/util/MonotonicGuard.h"

namespace cellutron {

/// @brief Utility to guard against out-of-order or stale messages.
/// @details Uses core dmq::util::MonotonicGuard for 64-bit safety.
/// @note This alias is NOT thread-safe. Use within a single named thread.
using MessageGuard = dmq::util::MonotonicGuard<uint64_t>;

} // namespace cellutron

#endif
