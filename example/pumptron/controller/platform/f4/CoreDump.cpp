/**
 * @file CoreDump.cpp
 * @brief Pumptron F4 core dump. See CoreDump.h.
 *
 * Everything on the store path runs with interrupts disabled, on whatever is
 * left of a possibly corrupted stack: no heap, no RTOS calls that block or take
 * locks, no printf. Output goes straight to SWV ITM.
 */

#include "CoreDump.h"
#include "messages/CoreDumpMsg.h"
#include "extras/util/crc16.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <cstddef>
#include <cstring>

using pumptron::CoreDumpMsg;
using pumptron::CoreDumpSource;

static_assert(CORE_DUMP_SRC_ASSERT     == static_cast<uint32_t>(CoreDumpSource::ASSERT),     "source mismatch");
static_assert(CORE_DUMP_SRC_HARD_FAULT == static_cast<uint32_t>(CoreDumpSource::HARD_FAULT), "source mismatch");
static_assert(CORE_DUMP_SRC_WATCHDOG   == static_cast<uint32_t>(CoreDumpSource::WATCHDOG),   "source mismatch");
static_assert(CORE_DUMP_SRC_RTOS       == static_cast<uint32_t>(CoreDumpSource::RTOS),       "source mismatch");

namespace {

constexpr uint32_t KEY_STORED = 0xBA5EBA11u;

/// Words of stack searched for return addresses (4 KB).
constexpr uint32_t MAX_STACK_SCAN_WORDS = 1024;

/// FreeRTOS fills every new task stack with this (tskSTACK_FILL_BYTE), so two
/// consecutive fill words mean the scan has walked past the live stack into
/// the untouched bottom of the next task stack.
constexpr uint32_t STACK_FILL_WORD = 0xA5A5A5A5u;

/// Crash record. Lives in .noinit so it survives the reset that follows a fault.
struct Record
{
    uint32_t key;
    uint32_t notKey;
    uint32_t source;
    char     buildId[CoreDumpMsg::BUILD_ID_LEN];
    char     where[CoreDumpMsg::WHERE_LEN];
    uint32_t line;
    uint32_t auxCode;
    uint32_t crashSeq;
    uint32_t uptimeMs;
    char     task[CoreDumpMsg::TASK_LEN];
    uint32_t regs[CoreDumpMsg::REG_COUNT];
    uint32_t faultRegs[CoreDumpMsg::FAULT_REG_COUNT];
    uint32_t callStackCount;
    uint32_t callStack[CoreDumpMsg::CALL_STACK_LEN];
    uint16_t crc;       ///< CRC of every byte before this field
};

__attribute__((section(".noinit"))) Record s_record;

/// Crashes stored since power-up. Also in .noinit so it counts across resets;
/// its own key tells a live count from power-on garbage. Together with the
/// uptime it makes every crash distinct, even a deterministic test crash that
/// is otherwise byte-identical every time, so the GUI's duplicate check only
/// ever drops a genuine resend of the same dump.
struct CrashCounter
{
    uint32_t key;
    uint32_t notKey;
    uint32_t count;
};

__attribute__((section(".noinit"))) CrashCounter s_crashCounter;

} // namespace

// Linker script symbols: end of code, top of RAM, GNU build ID note.
extern "C" const uint8_t _etext[];
extern "C" const uint8_t _estack[];
extern "C" const uint8_t g_build_id_note[];

namespace {

uint16_t RecordCrc(const Record& r)
{
    return dmq::util::Crc16CalcBlock(reinterpret_cast<const unsigned char*>(&r),
                                     static_cast<int>(offsetof(Record, crc)));
}

bool IsValid(const Record& r)
{
    return r.key == KEY_STORED && r.notKey == ~KEY_STORED && r.crc == RecordCrc(r);
}

bool IsCodeAddress(uint32_t addr)
{
    addr &= ~1u;    // Thumb bit
    return addr >= FLASH_BASE && addr < reinterpret_cast<uint32_t>(_etext);
}

bool IsRamAddress(uint32_t addr)
{
    const uint32_t ramEnd = reinterpret_cast<uint32_t>(_estack);
    return (addr >= SRAM1_BASE && addr < ramEnd) ||
           (addr >= CCMDATARAM_BASE && addr <= CCMDATARAM_END);
}

/// True if the `words` words starting at `p` are all in the same RAM block.
bool IsRamRange(const uint32_t* p, uint32_t words)
{
    const uint32_t first = reinterpret_cast<uint32_t>(p);
    const uint32_t last = first + words * 4u - 1u;
    return (first & 3u) == 0 && IsRamAddress(first) && IsRamAddress(last) &&
           ((first ^ last) & 0xF0000000u) == 0;
}

void CopyString(char* dst, size_t len, const char* src)
{
    size_t i = 0;
    if (src) {
        for (; i + 1 < len && src[i]; ++i)
            dst[i] = src[i];
    }
    dst[i] = '\0';
}

/// Keep the end of a long path ("...ump/PumpController.cpp"), not its start.
void CopyTail(char* dst, size_t len, const char* src)
{
    if (!src) { dst[0] = '\0'; return; }
    const size_t n = strlen(src);
    if (n < len) { CopyString(dst, len, src); return; }
    dst[0] = dst[1] = dst[2] = '.';
    CopyString(dst + 3, len - 3, src + n - (len - 4));
}

void AppendUnsigned(char* dst, size_t len, uint32_t value)
{
    char digits[11];
    int i = 0;
    do { digits[i++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    size_t pos = strlen(dst);
    while (i > 0 && pos + 1 < len)
        dst[pos++] = digits[--i];
    dst[pos] = '\0';
}

/// GNU build ID (from -Wl,--build-id) as hex, truncated to fit.
void CopyBuildId(char* dst, size_t len)
{
    // ELF note: namesz, descsz, type, name ("GNU\0"), desc (the ID bytes).
    const uint32_t* note = reinterpret_cast<const uint32_t*>(g_build_id_note);
    const uint32_t nameSize = note[0];
    const uint32_t descSize = note[1];
    const uint8_t* desc = g_build_id_note + 12 + ((nameSize + 3u) & ~3u);
    static const char HEX[] = "0123456789abcdef";
    size_t pos = 0;
    for (uint32_t i = 0; i < descSize && pos + 2 < len; ++i) {
        dst[pos++] = HEX[desc[i] >> 4];
        dst[pos++] = HEX[desc[i] & 0xF];
    }
    dst[pos] = '\0';
}

/// Name of what was running: a task, an ISR, or main() before the scheduler.
/// @param ipsr  Exception number that was active (0 = thread mode).
/// @param onPsp True if thread mode was using the process (task) stack.
void CopyActiveContext(char* dst, size_t len, uint32_t ipsr, bool onPsp)
{
    if (ipsr != 0) {
        CopyString(dst, len, "ISR ");
        AppendUnsigned(dst, len, ipsr);
    } else if (onPsp && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        CopyString(dst, len, pcTaskGetName(nullptr));
    } else {
        CopyString(dst, len, "main");
    }
}

/// True if `addr` is a Thumb return address: odd, inside the code range, and
/// directly after a call instruction -- a 32-bit BL (return = BL + 4) or a
/// 16-bit BLX Rm (return = BLX + 2). Rejects code addresses that are merely
/// data or stale values on the stack.
bool IsReturnAddress(uint32_t addr)
{
    if ((addr & 1u) == 0 || !IsCodeAddress(addr))
        return false;
    const uint32_t insnEnd = addr & ~1u;
    if (insnEnd < FLASH_BASE + 4u)
        return false;
    const uint16_t* before = reinterpret_cast<const uint16_t*>(insnEnd);

    // BLX Rm: 0100 0111 1xxx x000
    if ((before[-1] & 0xFF87u) == 0x4780u)
        return true;
    // BL: 1111 0xxx xxxx xxxx, then 11x1 xxxx xxxx xxxx
    return (before[-2] & 0xF800u) == 0xF000u && (before[-1] & 0xD000u) == 0xD000u;
}

void AddCallStackEntry(Record& r, uint32_t addr)
{
    if (r.callStackCount >= CoreDumpMsg::CALL_STACK_LEN)
        return;
    // Skip a repeat of the previous entry (e.g. a value copied down the stack).
    if (r.callStackCount > 0 && r.callStack[r.callStackCount - 1] == addr)
        return;
    r.callStack[r.callStackCount++] = addr;
}

/// Walk up the stack from `sp` collecting return addresses.
void ScanStack(Record& r, const uint32_t* sp)
{
    for (uint32_t i = 0; i < MAX_STACK_SCAN_WORDS; ++i) {
        if (r.callStackCount >= CoreDumpMsg::CALL_STACK_LEN || !IsRamRange(sp + i, 2))
            break;
        const uint32_t value = sp[i];
        if (value == STACK_FILL_WORD && sp[i + 1] == STACK_FILL_WORD)
            break;
        if (IsReturnAddress(value))
            AddCallStackEntry(r, value);
    }
}

inline const uint32_t* CurrentSp()
{
    const uint32_t* sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

void ItmWrite(const char* s)
{
    while (s && *s)
        ITM_SendChar(static_cast<uint32_t>(*s++));
}

uint32_t NextCrashSeq()
{
    CrashCounter& c = s_crashCounter;
    if (c.key != KEY_STORED || c.notKey != ~KEY_STORED) {
        c.key = KEY_STORED;
        c.notKey = ~KEY_STORED;
        c.count = 0;
    }
    return ++c.count;
}

void Store(uint32_t source, const char* where, uint32_t line, uint32_t aux,
           const uint32_t* frame, uint32_t excReturn)
{
    // First fault wins: never overwrite a record that has not been reported.
    if (IsValid(s_record))
        return;

    Record& r = s_record;
    memset(&r, 0, sizeof(r));
    r.source = source;
    r.line = line;
    r.auxCode = aux;
    r.crashSeq = NextCrashSeq();
    r.uptimeMs = HAL_GetTick();
    CopyTail(r.where, sizeof(r.where), where);
    CopyBuildId(r.buildId, sizeof(r.buildId));

    if (frame) {
        // Hardware exception: the CPU stacked R0-R3, R12, LR, PC, xPSR.
        for (uint32_t i = 0; i < CoreDumpMsg::REG_COUNT; ++i)
            r.regs[i] = frame[i];
        r.faultRegs[CoreDumpMsg::CFSR]  = SCB->CFSR;
        r.faultRegs[CoreDumpMsg::HFSR]  = SCB->HFSR;
        r.faultRegs[CoreDumpMsg::MMFAR] = SCB->MMFAR;
        r.faultRegs[CoreDumpMsg::BFAR]  = SCB->BFAR;

        const uint32_t xpsr = frame[CoreDumpMsg::XPSR];
        CopyActiveContext(r.task, sizeof(r.task), xpsr & 0x1FFu, (excReturn & 0x4u) != 0);

        // The faulting instruction and its caller come first, then the stack
        // above the exception frame: 8 words, or 26 with FPU state
        // (EXC_RETURN bit 4 clear), plus one if the CPU padded for alignment.
        if (IsCodeAddress(frame[CoreDumpMsg::PC]))
            AddCallStackEntry(r, frame[CoreDumpMsg::PC] | 1u);
        if (IsReturnAddress(frame[CoreDumpMsg::LR]))
            AddCallStackEntry(r, frame[CoreDumpMsg::LR]);
        uint32_t frameWords = (excReturn & 0x10u) ? 8u : 26u;
        if (xpsr & (1u << 9))
            frameWords++;
        ScanStack(r, frame + frameWords);
    } else {
        CopyActiveContext(r.task, sizeof(r.task), __get_IPSR(), (__get_CONTROL() & 0x2u) != 0);
        ScanStack(r, CurrentSp());
    }

    r.key = KEY_STORED;
    r.notKey = ~KEY_STORED;
    r.crc = RecordCrc(r);
}

__attribute__((noreturn)) void Reset()
{
    ItmWrite("*** Core dump stored, resetting ***\r\n");
    __DSB();
    NVIC_SystemReset();
    for (;;) {}
}

} // namespace

extern "C" void CoreDump_EnableFaultHandlers(void)
{
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;
}

extern "C" void CoreDump_StoreAndReset(uint32_t source, const char* where, uint32_t line, uint32_t aux)
{
    __disable_irq();
    ItmWrite("\r\n*** FAULT: ");
    ItmWrite(where);
    ItmWrite(" ***\r\n");
    Store(source, where, line, aux, nullptr, 0);
    Reset();
}

extern "C" void CoreDump_FaultEntry(uint32_t* frame, uint32_t excReturn)
{
    __disable_irq();

    static const char* const NAMES[] = { "?", "?", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault" };
    const uint32_t vector = __get_IPSR() & 0x1FFu;
    const char* name = vector < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[vector] : "Exception";
    ItmWrite("\r\n*** ");
    ItmWrite(name);
    ItmWrite(" ***\r\n");

    // A stack overflow can leave the stack pointer outside RAM; don't fault
    // again reading the frame, just record the exception without it.
    if (!IsRamRange(frame, 8))
        frame = nullptr;
    Store(CORE_DUMP_SRC_HARD_FAULT, name, 0, vector, frame, excReturn);
    Reset();
}

extern "C" bool CoreDump_IsStored(void)
{
    return IsValid(s_record);
}

extern "C" void CoreDump_Clear(void)
{
    s_record.key = 0;
    s_record.notKey = 0;
}

bool CoreDump_ToMsg(CoreDumpMsg& msg)
{
    const Record& r = s_record;
    if (!IsValid(r))
        return false;

    msg.source = static_cast<CoreDumpSource>(r.source);
    memcpy(msg.buildId, r.buildId, sizeof(msg.buildId));
    memcpy(msg.where, r.where, sizeof(msg.where));
    msg.line = r.line;
    msg.auxCode = r.auxCode;
    msg.crashSeq = r.crashSeq;
    msg.uptimeMs = r.uptimeMs;
    memcpy(msg.task, r.task, sizeof(msg.task));
    memcpy(msg.regs, r.regs, sizeof(msg.regs));
    memcpy(msg.faultRegs, r.faultRegs, sizeof(msg.faultRegs));
    msg.callStackCount = static_cast<uint8_t>(r.callStackCount);
    memcpy(msg.callStack, r.callStack, sizeof(msg.callStack));
    return true;
}
