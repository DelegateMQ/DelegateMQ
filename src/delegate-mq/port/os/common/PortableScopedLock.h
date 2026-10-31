#ifndef _PORTABLE_SCOPED_LOCK_H
#define _PORTABLE_SCOPED_LOCK_H

/// @file PortableScopedLock.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Multi-mutex lock for toolchains without std::scoped_lock.
///
/// @details Some bare-metal standard libraries, built without thread support
/// (e.g. Arm GNU Toolchain 14), provide <mutex> but no std::scoped_lock.
/// DelegateOpt.h selects this class as dmq::ScopedLock on those toolchains only;
/// everywhere else dmq::ScopedLock is std::scoped_lock.
///
/// Locks the mutexes in address order, so two locks over the same mutexes can't
/// deadlock, and unlocks in reverse order. A mutex passed twice is locked once.
/// All mutexes must be the same type (every DelegateMQ use is).

#include <cstddef>
#include <functional>
#include <type_traits>

namespace dmq::os {

template <typename M, typename... Rest>
class PortableScopedLock {
    static_assert((std::is_same_v<M, Rest> && ...),
        "PortableScopedLock: all mutexes must be the same type");
    static constexpr std::size_t N = 1 + sizeof...(Rest);
    M* m_mutexes[N];

    // A mutex passed twice sorts next to its duplicate.
    bool IsDuplicate(std::size_t i) const noexcept {
        return i > 0 && m_mutexes[i] == m_mutexes[i - 1];
    }

public:
    explicit PortableScopedLock(M& first, Rest&... rest) : m_mutexes{ &first, &rest... } {
        for (std::size_t i = 1; i < N; i++) {               // insertion sort by address
            M* m = m_mutexes[i];
            std::size_t j = i;
            for (; j > 0 && std::less<M*>()(m, m_mutexes[j - 1]); j--)
                m_mutexes[j] = m_mutexes[j - 1];
            m_mutexes[j] = m;
        }
        for (std::size_t i = 0; i < N; i++)
            if (!IsDuplicate(i)) m_mutexes[i]->lock();
    }

    ~PortableScopedLock() {
        for (std::size_t i = N; i-- > 0;)
            if (!IsDuplicate(i)) m_mutexes[i]->unlock();
    }

    PortableScopedLock(const PortableScopedLock&) = delete;
    PortableScopedLock& operator=(const PortableScopedLock&) = delete;
};

} // namespace dmq::os

#endif // _PORTABLE_SCOPED_LOCK_H
