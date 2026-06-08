#include "extras/util/Fault.h"
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32) || defined(__linux__)
#include <iostream>
#include <iomanip>
#include "delegate/DelegateOpt.h"
#endif

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <vector>
#pragma comment(lib, "dbghelp.lib")
#endif

#ifdef __linux__
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#endif

namespace dmq::util {

#ifdef __linux__
static void LinuxSignalHandler(int sig) {
    const char* name = "Unknown Signal";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV (Segmentation Fault)"; break;
        case SIGABRT: name = "SIGABRT (Abort)"; break;
        case SIGFPE:  name = "SIGFPE (Floating Point Exception)"; break;
        case SIGILL:  name = "SIGILL (Illegal Instruction)"; break;
        case SIGBUS:  name = "SIGBUS (Bus Error)"; break;
        case SIGTERM: name = "SIGTERM (Termination Request)"; break;
    }
    FaultHandler(name, (unsigned short)sig);
}

void InstallCrashHandlers() {
    std::signal(SIGSEGV, LinuxSignalHandler);
    std::signal(SIGABRT, LinuxSignalHandler);
    std::signal(SIGFPE,  LinuxSignalHandler);
    std::signal(SIGILL,  LinuxSignalHandler);
    std::signal(SIGBUS,  LinuxSignalHandler);
    std::signal(SIGTERM, LinuxSignalHandler);
}
#endif

#ifdef _WIN32
LONG WINAPI WindowsExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
    (void)pExceptionPointers;
    FaultHandler("Windows Unhandled Exception", 0);
}

void InstallCrashHandlers() {
    SetUnhandledExceptionFilter(WindowsExceptionFilter);
}
#endif

#if !defined(_WIN32) && !defined(__linux__)
void InstallCrashHandlers() {
    // Stub for other platforms
}
#endif

#ifdef _WIN32
static void PrintStackTraceWindows(HANDLE hThread, const char* threadName) {

    DWORD tid = GetThreadId(hThread);
    printf("\n--- Stack Trace: %s (TID: %lu) ---\n", (threadName ? threadName : "Unknown"), tid);
    fflush(stdout);

    CONTEXT context = { 0 };
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(hThread, &context)) {
        printf("  Failed to get thread context (Error: %lu)\n", GetLastError());
        fflush(stdout);
        return;
    }

    STACKFRAME64 stackFrame = { 0 };
#ifdef _M_IX86
    DWORD machineType = IMAGE_FILE_MACHINE_I386;
    stackFrame.AddrPC.Offset = context.Eip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Ebp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Esp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
    stackFrame.AddrPC.Offset = context.Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Rsp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#else
    return;
#endif

    HANDLE hProcess = GetCurrentProcess();
    int frameCount = 0;
    while (StackWalk64(machineType, hProcess, hThread, &stackFrame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
        if (stackFrame.AddrPC.Offset == 0 || frameCount++ > 50) break;

        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        if (SymFromAddr(hProcess, stackFrame.AddrPC.Offset, &displacement, pSymbol)) {
            IMAGEHLP_LINE64 line = { 0 };
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(hProcess, stackFrame.AddrPC.Offset, &lineDisplacement, &line)) {
                printf("  %s [%s:%lu]\n", pSymbol->Name, line.FileName, line.LineNumber);
            } else {
                printf("  %s (no line info)\n", pSymbol->Name);
            }
        } else {
            printf("  0x%llX (no symbol info)\n", stackFrame.AddrPC.Offset);
        }
        fflush(stdout);
    }
}

static void PrintAllThreadsStackTraces() {
    printf("\nCapturing stack traces for all threads...\n");
    fflush(stdout);

    HANDLE hProcess = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    
    // Use FALSE for fInvadeProcess to avoid Error 87 in some environments.
    // Symbols will be loaded on-demand.
    if (!SymInitialize(hProcess, NULL, FALSE)) {
        printf("SymInitialize failed (Error: %lu)\n", GetLastError());
        fflush(stdout);
        return;
    }

    // Refresh modules manually since we didn't invade
    SymRefreshModuleList(hProcess);

    DWORD currentProcessId = GetCurrentProcessId();
    DWORD currentThreadId = GetCurrentThreadId();
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

    if (hSnapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te = { 0 };
        te.dwSize = sizeof(THREADENTRY32);

        if (Thread32First(hSnapshot, &te)) {
            do {
                if (te.th32OwnerProcessID == currentProcessId) {
                    if (te.th32ThreadID == currentThreadId) {
                        PrintStackTraceWindows(GetCurrentThread(), "Fault/Watchdog Thread");
                    } else {
                        HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                        if (hThread) {
                            SuspendThread(hThread);
                            PrintStackTraceWindows(hThread, "Suspended Thread");
                            ResumeThread(hThread);
                            CloseHandle(hThread);
                        }
                    }
                }
            } while (Thread32Next(hSnapshot, &te));
        }
        CloseHandle(hSnapshot);
    }
    SymCleanup(hProcess);
    printf("\nStack trace capture complete.\n");
    fflush(stdout);
}
#endif

#ifdef __linux__
static void PrintStackTraceLinux() {
    void* array[50];
    int size = backtrace(array, 50);
    char** symbols = backtrace_symbols(array, size);
    printf("\n--- Stack Trace (Current Thread) ---\n");
    for (int i = 0; i < size; i++) {
        printf("  %s\n", symbols[i]);
    }
    fflush(stdout);
    free(symbols);
}
#endif

//----------------------------------------------------------------------------
// FaultHandler
//----------------------------------------------------------------------------
DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    // 1. PRINT FIRST
    printf("\n************************************************\n");
    printf("FaultHandler called. Application terminated.\n");
    printf("File: %s Line: %u\n", file, static_cast<unsigned int>(line));
    printf("************************************************\n");
    fflush(stdout);

#if defined(_WIN32) || defined(__linux__)
    LOG_ERROR("FaultHandler File={} Line={}", file, line);
#endif

    // 2. PRINT STACK TRACES
#ifdef _WIN32
    PrintAllThreadsStackTraces();
#elif defined(__linux__)
    PrintStackTraceLinux();
#endif

    // 3. Break only if interactive or specifically desired
#ifdef _WIN32
    if (IsDebuggerPresent()) {
        DebugBreak();
    }
#endif

    // 4. Force exit
#if defined(__linux__)
    printf("\nTerminating application...\n");
    fflush(stdout);

    // When running via terminal wrappers (like xterm -e), the console closes 
    // immediately on exit. Wait for input to allow the user to read the crash info.
    // Note: isatty() can return false when stdin is redirected by the wrapper.
    printf("Press Enter to exit (or wait 60s)...");
    fflush(stdout);

    // Set a timeout on stdin so we don't hang forever in CI/headless
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 60;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        (void)getchar();
    }
    
    abort();
#elif defined(_WIN32)
    printf("\nTerminating application...\n");
    fflush(stdout);
    abort();
#else
    printf("FaultHandler: %s line %u\r\n", file, static_cast<unsigned int>(line));
    fflush(stdout);
    while(1);
#endif
}

} // namespace dmq::util

extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    dmq::util::FaultHandler(file, line);
}

extern "C" void InstallCrashHandlers()
{
    dmq::util::InstallCrashHandlers();
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
#if defined(_WIN32) || defined(__linux__)
    std::cout << "\n************************************************" << std::endl;
    std::cout << "WATCHDOG EXPIRED: " << threadName << std::endl;
    std::cout << "************************************************\n" << std::endl;
    ASSERT();
#else
    printf("WATCHDOG EXPIRED: %s\r\n", threadName);
    while (1);
#endif
}
