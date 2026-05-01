#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include "extras/util/MonotonicGuard.h"
#include <iostream>
#include <limits>

using namespace dmq::util;

// ---- uint64_t (MessageGuard / sequence-counter path: simple > comparison, no rollover handling) ----

static void MonotonicGuard_U64_FirstCallAlwaysTrue()
{
    MonotonicGuard<uint64_t> g;
    ASSERT_TRUE(g.IsNewer(0));
    std::cout << "MonotonicGuard_U64_FirstCallAlwaysTrue() complete!" << std::endl;
}

static void MonotonicGuard_U64_IncreasingReturnsTrue()
{
    MonotonicGuard<uint64_t> g;
    ASSERT_TRUE(g.IsNewer(100));
    ASSERT_TRUE(g.IsNewer(101));
    ASSERT_TRUE(g.IsNewer(999));
    ASSERT_TRUE(g.IsNewer(std::numeric_limits<uint64_t>::max() - 1));
    std::cout << "MonotonicGuard_U64_IncreasingReturnsTrue() complete!" << std::endl;
}

static void MonotonicGuard_U64_DuplicateReturnsFalse()
{
    MonotonicGuard<uint64_t> g;
    ASSERT_TRUE(g.IsNewer(50));
    ASSERT_TRUE(!g.IsNewer(50));  // same timestamp
    std::cout << "MonotonicGuard_U64_DuplicateReturnsFalse() complete!" << std::endl;
}

static void MonotonicGuard_U64_DecreasingReturnsFalse()
{
    MonotonicGuard<uint64_t> g;
    ASSERT_TRUE(g.IsNewer(500));
    ASSERT_TRUE(!g.IsNewer(499));
    ASSERT_TRUE(!g.IsNewer(0));
    std::cout << "MonotonicGuard_U64_DecreasingReturnsFalse() complete!" << std::endl;
}

static void MonotonicGuard_U64_ResetRestoresFirstCallBehavior()
{
    MonotonicGuard<uint64_t> g;
    ASSERT_TRUE(g.IsNewer(200));
    ASSERT_TRUE(!g.IsNewer(100));   // stale before reset
    g.Reset();
    ASSERT_TRUE(g.IsNewer(100));    // first call after reset always succeeds
    ASSERT_TRUE(g.IsNewer(101));
    ASSERT_TRUE(!g.IsNewer(100));   // back to normal monotonic behaviour
    std::cout << "MonotonicGuard_U64_ResetRestoresFirstCallBehavior() complete!" << std::endl;
}

static void MonotonicGuard_U64_FirstCallWithMaxValue()
{
    // First call must accept even the largest possible timestamp.
    MonotonicGuard<uint64_t> g;
    ASSERT_TRUE(g.IsNewer(std::numeric_limits<uint64_t>::max()));
    ASSERT_TRUE(!g.IsNewer(std::numeric_limits<uint64_t>::max()));  // duplicate
    std::cout << "MonotonicGuard_U64_FirstCallWithMaxValue() complete!" << std::endl;
}

// ---- uint32_t (rollover path: signed-difference comparison) ----

static void MonotonicGuard_U32_NormalIncreasing()
{
    MonotonicGuard<uint32_t> g;
    ASSERT_TRUE(g.IsNewer(1000));
    ASSERT_TRUE(g.IsNewer(1001));
    ASSERT_TRUE(!g.IsNewer(1001));  // duplicate
    ASSERT_TRUE(!g.IsNewer(500));   // stale
    std::cout << "MonotonicGuard_U32_NormalIncreasing() complete!" << std::endl;
}

static void MonotonicGuard_U32_RolloverDetected()
{
    // Signed-difference handles rollover up to half the type range (~24 days for ms).
    // Simulate: last = near max, new = small value (wrapped around).
    MonotonicGuard<uint32_t> g;
    const uint32_t nearMax = std::numeric_limits<uint32_t>::max() - 10;
    ASSERT_TRUE(g.IsNewer(nearMax));
    // After rollover, new value is small but signed diff is positive
    ASSERT_TRUE(g.IsNewer(5));
    std::cout << "MonotonicGuard_U32_RolloverDetected() complete!" << std::endl;
}

static void MonotonicGuard_U32_StaleAfterRollover()
{
    // A value just below nearMax should be treated as stale (went backwards) after rollover.
    MonotonicGuard<uint32_t> g;
    const uint32_t nearMax = std::numeric_limits<uint32_t>::max() - 10;
    ASSERT_TRUE(g.IsNewer(nearMax));
    ASSERT_TRUE(g.IsNewer(5));           // rollover accepted
    ASSERT_TRUE(!g.IsNewer(4));          // duplicate/stale after rollover
    ASSERT_TRUE(!g.IsNewer(3));
    std::cout << "MonotonicGuard_U32_StaleAfterRollover() complete!" << std::endl;
}

// ---- uint8_t (smallest supported type, also uses signed-difference path) ----

static void MonotonicGuard_U8_RolloverDetected()
{
    MonotonicGuard<uint8_t> g;
    ASSERT_TRUE(g.IsNewer(250));
    ASSERT_TRUE(g.IsNewer(5));   // rolled over; signed diff = 5 - 250 as int8 = positive
    ASSERT_TRUE(!g.IsNewer(4));  // stale
    std::cout << "MonotonicGuard_U8_RolloverDetected() complete!" << std::endl;
}

static void MonotonicGuard_U8_NoFalseRollover()
{
    // The signed-difference trick is valid within half the type range (127 for uint8_t).
    // A delta of exactly 128 wraps to int8_t(-128), which is not > 0 — rejected as stale.
    MonotonicGuard<uint8_t> g;
    ASSERT_TRUE(g.IsNewer(10));
    // delta = 128; static_cast<int8_t>(128) = -128 → rejected
    ASSERT_TRUE(!g.IsNewer(static_cast<uint8_t>(10 + 128)));  // = 138
    std::cout << "MonotonicGuard_U8_NoFalseRollover() complete!" << std::endl;
}

// ---- Reset edge cases ----

static void MonotonicGuard_ResetFromZero()
{
    MonotonicGuard<uint64_t> g;
    // Default state: first is true, so first call at 0 succeeds.
    ASSERT_TRUE(g.IsNewer(0));
    ASSERT_TRUE(!g.IsNewer(0));
    g.Reset();
    // After reset, 0 must succeed again.
    ASSERT_TRUE(g.IsNewer(0));
    std::cout << "MonotonicGuard_ResetFromZero() complete!" << std::endl;
}

void MonotonicGuardTests()
{
    // uint64_t (MessageGuard) path
    MonotonicGuard_U64_FirstCallAlwaysTrue();
    MonotonicGuard_U64_IncreasingReturnsTrue();
    MonotonicGuard_U64_DuplicateReturnsFalse();
    MonotonicGuard_U64_DecreasingReturnsFalse();
    MonotonicGuard_U64_ResetRestoresFirstCallBehavior();
    MonotonicGuard_U64_FirstCallWithMaxValue();

    // uint32_t rollover path
    MonotonicGuard_U32_NormalIncreasing();
    MonotonicGuard_U32_RolloverDetected();
    MonotonicGuard_U32_StaleAfterRollover();

    // uint8_t (smallest type, signed-difference edge cases)
    MonotonicGuard_U8_RolloverDetected();
    MonotonicGuard_U8_NoFalseRollover();

    // Reset edge cases
    MonotonicGuard_ResetFromZero();

    std::cout << "MonotonicGuardTests() complete!" << std::endl;
}
