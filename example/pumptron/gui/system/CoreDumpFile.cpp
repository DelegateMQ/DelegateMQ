#include "CoreDumpFile.h"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace pumptron {

namespace {

std::string Hex(uint32_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08x", static_cast<unsigned>(value));
    return buf;
}

struct FaultBit { uint32_t mask; const char* name; };

/// Set bits of the Cortex-M4 fault status registers, by name.
std::string DecodeBits(uint32_t value, const FaultBit* bits, size_t count)
{
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        if (value & bits[i].mask) {
            if (!out.empty()) out += " ";
            out += bits[i].name;
        }
    }
    return out.empty() ? "-" : out;
}

constexpr uint32_t CFSR_MMARVALID = 1u << 7;
constexpr uint32_t CFSR_BFARVALID = 1u << 15;

const FaultBit CFSR_BITS[] = {
    { 1u << 0,  "IACCVIOL" },  { 1u << 1,  "DACCVIOL" },   { 1u << 3,  "MUNSTKERR" },
    { 1u << 4,  "MSTKERR" },   { 1u << 5,  "MLSPERR" },    { CFSR_MMARVALID, "MMARVALID" },
    { 1u << 8,  "IBUSERR" },   { 1u << 9,  "PRECISERR" },  { 1u << 10, "IMPRECISERR" },
    { 1u << 11, "UNSTKERR" },  { 1u << 12, "STKERR" },     { 1u << 13, "LSPERR" },
    { CFSR_BFARVALID, "BFARVALID" },
    { 1u << 16, "UNDEFINSTR" }, { 1u << 17, "INVSTATE" },  { 1u << 18, "INVPC" },
    { 1u << 19, "NOCP" },      { 1u << 24, "UNALIGNED" },  { 1u << 25, "DIVBYZERO" },
};

const FaultBit HFSR_BITS[] = {
    { 1u << 1, "VECTTBL" }, { 1u << 30, "FORCED" }, { 1u << 31, "DEBUGEVT" },
};

} // namespace

std::string FormatCoreDump(const CoreDumpMsg& msg)
{
    std::ostringstream os;
    const bool hardFault = msg.source == CoreDumpSource::HARD_FAULT;

    os << "Type: " << ToString(msg.source);
    if (msg.source == CoreDumpSource::RTOS)
        os << " (" << ToString(static_cast<RtosFault>(msg.auxCode)) << ")";
    os << "\n";

    switch (msg.source) {
        case CoreDumpSource::ASSERT:
            os << "File: " << msg.where << "\n"
               << "Line: " << msg.line << "\n";
            break;
        case CoreDumpSource::HARD_FAULT:
            os << "Exception: " << msg.where << " (vector " << msg.auxCode << ")\n";
            break;
        case CoreDumpSource::WATCHDOG:
            os << "Unresponsive thread: " << msg.where << "\n";
            break;
        case CoreDumpSource::RTOS:
            if (static_cast<RtosFault>(msg.auxCode) == RtosFault::STACK_OVERFLOW)
                os << "Task: " << msg.where << "\n";
            else if (msg.line != 0)
                os << "File: " << msg.where << "\n" << "Line: " << msg.line << "\n";
            break;
    }
    os << "Active context: " << msg.task << "\n"
       << "Build ID: " << msg.buildId << "\n";

    if (hardFault) {
        static const char* const REG_NAMES[CoreDumpMsg::REG_COUNT] =
            { "R0", "R1", "R2", "R3", "R12", "LR", "PC", "xPSR" };
        os << "\n";
        for (size_t i = 0; i < CoreDumpMsg::REG_COUNT; ++i)
            os << REG_NAMES[i] << ": " << Hex(msg.regs[i]) << "\n";

        const uint32_t cfsr = msg.faultRegs[CoreDumpMsg::CFSR];
        const uint32_t hfsr = msg.faultRegs[CoreDumpMsg::HFSR];
        os << "CFSR: " << Hex(cfsr) << "  "
           << DecodeBits(cfsr, CFSR_BITS, sizeof(CFSR_BITS) / sizeof(CFSR_BITS[0])) << "\n"
           << "HFSR: " << Hex(hfsr) << "  "
           << DecodeBits(hfsr, HFSR_BITS, sizeof(HFSR_BITS) / sizeof(HFSR_BITS[0])) << "\n";
        if (cfsr & CFSR_MMARVALID)
            os << "MMFAR: " << Hex(msg.faultRegs[CoreDumpMsg::MMFAR]) << "  (faulting data address)\n";
        if (cfsr & CFSR_BFARVALID)
            os << "BFAR: " << Hex(msg.faultRegs[CoreDumpMsg::BFAR]) << "  (faulting data address)\n";
    }

    // For a hardware exception, entry 0 is the faulting PC itself when the PC
    // was in flash. Every other entry is a Thumb return address (bit 0 set):
    // clear the Thumb bit and step back one byte to land inside the call
    // instruction. Decoding the address after the call instead can name the
    // wrong line, or the next function when the call was to a noreturn function.
    os << "\nCall stack (innermost first):\n";
    std::string decodeArgs;
    for (uint8_t i = 0; i < msg.callStackCount; ++i) {
        const uint32_t addr = msg.callStack[i];
        const bool isPc = hardFault && i == 0 && addr == (msg.regs[CoreDumpMsg::PC] | 1u);
        os << "  " << static_cast<int>(i) << ": " << Hex(addr) << (isPc ? "  (PC)" : "") << "\n";
        decodeArgs += " " + Hex(isPc ? (addr & ~1u) : ((addr & ~1u) - 1));
    }
    if (msg.callStackCount == 0)
        os << "  (none captured)\n";
    else
        os << "\nDecode (with the pumptron_f4.elf whose build ID matches):\n"
           << "  arm-none-eabi-addr2line -f -C -p -e pumptron_f4.elf" << decodeArgs << "\n";

    return os.str();
}

bool WriteCoreDumpFile(const std::string& body, std::string& path)
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);
    char received[32];
    std::strftime(received, sizeof(received), "%Y-%m-%d %H:%M:%S", &local);

    // Never overwrite: a second dump in the same second gets a suffix.
    path = std::string("dump_") + stamp + ".txt";
    for (int n = 2; std::ifstream(path).good(); ++n)
        path = std::string("dump_") + stamp + "_" + std::to_string(n) + ".txt";

    std::ofstream file(path);
    if (!file)
        return false;
    file << "Pumptron controller core dump\n"
         << "Received: " << received << "\n"
         << body;
    return static_cast<bool>(file);
}

} // namespace pumptron
