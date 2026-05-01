#ifndef MONOTONIC_GUARD_H
#define MONOTONIC_GUARD_H

#include <cstdint>
#include <type_traits>

namespace dmq::util {

/// @brief Utility to guard against out-of-order or stale messages.
/// @details Uses 64-bit timestamps to provide permanent monotonic safety.
/// @note This class is NOT thread-safe. It is intended to be used within a single 
/// thread of control (e.g., an Active Object or a single subscriber thread).
template <typename T>
class MonotonicGuard {
    static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>, "T must be an unsigned integral type");

public:
    MonotonicGuard() = default;

    /// @brief Check if a message is newer (more recent than the last processed).
    /// @param timestamp The timestamp from the incoming message.
    /// @return true if newer, false if stale or out-of-order.
    bool IsNewer(T timestamp) {
        if (m_first) {
            m_lastTimestamp = timestamp;
            m_first = false;
            return true;
        }

        if constexpr (sizeof(T) <= 4) {
            // Use signed difference to handle rollover for 32-bit (or smaller) types.
            // This works correctly up to half the range of the type (e.g., ~24 days for uint32 ms).
            using SignedT = std::make_signed_t<T>;
            SignedT diff = static_cast<SignedT>(timestamp - m_lastTimestamp);
            if (diff > 0) {
                m_lastTimestamp = timestamp;
                return true;
            }
        } else {
            // For 64-bit types, rollover is effectively impossible.
            // Simple greater-than comparison is sufficient.
            if (timestamp > m_lastTimestamp) {
                m_lastTimestamp = timestamp;
                return true;
            }
        }
        
        return false;
    }

    /// @brief Reset the guard to its initial state.
    void Reset() { 
        m_first = true; 
        m_lastTimestamp = 0; 
    }

private:
    T m_lastTimestamp = 0;
    bool m_first = true;
};

} // namespace dmq::util

#endif
