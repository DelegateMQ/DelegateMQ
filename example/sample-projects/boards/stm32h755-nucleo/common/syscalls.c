/// @file syscalls.c
/// newlib system calls for both cores. stdout and stderr go to BoardWrite()
/// (the Cortex-M7's UART; the Cortex-M4 has no output yet). The heap grows
/// from the end of .bss up to the linker script's reserved main stack.

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

/// Write len bytes of console output. Weak: a core without output drops it.
__attribute__((weak)) int BoardWrite(const char* buf, int len)
{
    (void)buf;
    return len;
}

int _write(int fd, const char* buf, int len)
{
    (void)fd;
    return BoardWrite(buf, len);
}

int _read(int fd, char* buf, int len)   { (void)fd; (void)buf; (void)len; return 0; }
int _close(int fd)                      { (void)fd; return -1; }
int _lseek(int fd, int ptr, int dir)    { (void)fd; (void)ptr; (void)dir; return 0; }
int _isatty(int fd)                     { (void)fd; return 1; }  // stdout line buffered
int _getpid(void)                       { return 1; }
int _kill(int pid, int sig)             { (void)pid; (void)sig; errno = EINVAL; return -1; }

int _fstat(int fd, struct stat* st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

void _exit(int status)
{
    (void)status;
    for (;;) {}
}

// From the linker script: end of .bss, top of RAM, the main stack reserve
extern uint8_t end;
extern uint8_t _estack;
extern uint32_t _Min_Stack_Size;

void* _sbrk(ptrdiff_t incr)
{
    static uint8_t* heapEnd = 0;
    const uint8_t* limit = &_estack - (uintptr_t)&_Min_Stack_Size;  // a linker symbol's value is its address
    if (heapEnd == 0)
        heapEnd = &end;
    // Compared as distances: heapEnd + incr could overflow for a huge incr
    if (incr > limit - heapEnd || incr < &end - heapEnd) {
        errno = ENOMEM;
        return (void*)-1;
    }
    uint8_t* prev = heapEnd;
    heapEnd += incr;
    return prev;
}
