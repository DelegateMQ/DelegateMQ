#ifndef PUMPTRON_CORE_DUMP_MSG_H
#define PUMPTRON_CORE_DUMP_MSG_H

#include "MessageBase.h"
#include <cstring>

namespace pumptron {

/// @brief What stored a core dump.
enum class CoreDumpSource : uint8_t {
    ASSERT = 0,     ///< DMQ_ASSERT / BAD_ALLOC (DelegateMQ FaultHandler)
    HARD_FAULT,     ///< Cortex-M HardFault / MemManage / BusFault / UsageFault
    WATCHDOG,       ///< dmq::os::Thread watchdog expired
    RTOS            ///< FreeRTOS fault; auxCode holds an RtosFault
};

/// @brief auxCode values for CoreDumpSource::RTOS.
enum class RtosFault : uint32_t {
    CONFIG_ASSERT = 1,  ///< configASSERT failed (where/line = FreeRTOS source)
    STACK_OVERFLOW,     ///< Stack overflow (where = task name)
    HEAP_EXHAUSTED      ///< pvPortMalloc failed
};

inline const char* ToString(CoreDumpSource s) {
    switch (s) {
        case CoreDumpSource::ASSERT:     return "Software assertion";
        case CoreDumpSource::HARD_FAULT: return "Hardware exception";
        case CoreDumpSource::WATCHDOG:   return "Thread watchdog expired";
        case CoreDumpSource::RTOS:       return "FreeRTOS fault";
    }
    return "Unknown";
}

inline const char* ToString(RtosFault f) {
    switch (f) {
        case RtosFault::CONFIG_ASSERT:  return "configASSERT";
        case RtosFault::STACK_OVERFLOW: return "stack overflow";
        case RtosFault::HEAP_EXHAUSTED: return "heap exhausted";
    }
    return "unknown";
}

/// @brief Crash record from the controller's previous run (controller -> GUI).
///        Sent RELIABLE, once, after a reboot that found a stored core dump.
/// @details Addresses are raw; decode them with addr2line against the exact
///          firmware image named by `buildId`.
struct CoreDumpMsg : public MessageBase
{
    static constexpr size_t BUILD_ID_LEN   = 24;
    static constexpr size_t WHERE_LEN      = 64;
    static constexpr size_t TASK_LEN       = 16;
    static constexpr size_t CALL_STACK_LEN = 12;

    /// Stacked exception frame order (valid for HARD_FAULT only).
    enum Reg { R0, R1, R2, R3, R12, LR, PC, XPSR, REG_COUNT };
    /// System Control Block fault status (valid for HARD_FAULT only).
    enum FaultReg { CFSR, HFSR, MMFAR, BFAR, FAULT_REG_COUNT };

    CoreDumpSource source = CoreDumpSource::ASSERT;
    char     buildId[BUILD_ID_LEN] = {};  ///< Firmware build that crashed
    char     where[WHERE_LEN] = {};       ///< Source file (tail) or thread/task name
    uint32_t line = 0;                    ///< Source line, 0 if none
    uint32_t auxCode = 0;                 ///< Source-specific (e.g. exception number)
    uint32_t crashSeq = 0;                ///< Crash number since power-up (1, 2, ...)
    uint32_t uptimeMs = 0;                ///< Time since boot when the fault occurred
    char     task[TASK_LEN] = {};         ///< Active task or ISR at the time of the fault
    uint32_t regs[REG_COUNT] = {};
    uint32_t faultRegs[FAULT_REG_COUNT] = {};
    uint8_t  callStackCount = 0;
    uint32_t callStack[CALL_STACK_LEN] = {};   ///< Return addresses, innermost first

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        uint8_t s = 0;
        ms.read(is, s);
        source = static_cast<CoreDumpSource>(s);
        ms.read(is, buildId, sizeof(buildId));
        ms.read(is, where, sizeof(where));
        ms.read(is, line);
        ms.read(is, auxCode);
        ms.read(is, crashSeq);
        ms.read(is, uptimeMs);
        ms.read(is, task, sizeof(task));
        for (auto& r : regs)      ms.read(is, r);
        for (auto& r : faultRegs) ms.read(is, r);
        ms.read(is, callStackCount);
        if (callStackCount > CALL_STACK_LEN)
            callStackCount = CALL_STACK_LEN;
        for (uint8_t i = 0; i < callStackCount; ++i)
            ms.read(is, callStack[i]);
        buildId[BUILD_ID_LEN - 1] = where[WHERE_LEN - 1] = task[TASK_LEN - 1] = '\0';
        return is;
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        uint8_t s = static_cast<uint8_t>(source);
        ms.write(os, s);
        ms.write(os, static_cast<const char*>(buildId));
        ms.write(os, static_cast<const char*>(where));
        ms.write(os, line);
        ms.write(os, auxCode);
        ms.write(os, crashSeq);
        ms.write(os, uptimeMs);
        ms.write(os, static_cast<const char*>(task));
        for (auto& r : regs)      ms.write(os, r);
        for (auto& r : faultRegs) ms.write(os, r);
        ms.write(os, callStackCount);
        for (uint8_t i = 0; i < callStackCount && i < CALL_STACK_LEN; ++i)
            ms.write(os, callStack[i]);
        return os;
    }
};

} // namespace pumptron

#endif
