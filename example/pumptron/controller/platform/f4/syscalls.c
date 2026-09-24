/**
 * @file syscalls.c
 * @brief Minimal newlib syscalls for the Pumptron F4 target.
 *
 * printf()/stdout -> SWV ITM stimulus port 0. View in STM32CubeIDE: Debug
 * Configuration -> Debugger -> Serial Wire Viewer (core clock 168 MHz), then
 * the SWV ITM Data Console with port 0 enabled.
 *
 * There is no filesystem or process model; everything else is a stub.
 * _sbrk comes from --specs=nosys.specs (only newlib-internal buffers use it --
 * C++ new/delete go to the FreeRTOS heap, see main.cpp).
 */

#include "stm32f4xx.h"
#include <errno.h>
#include <sys/stat.h>

int _write(int file, char* ptr, int len)
{
    (void)file;
    for (int i = 0; i < len; ++i)
        ITM_SendChar((uint32_t)ptr[i]);
    return len;
}

int _read(int file, char* ptr, int len)   { (void)file; (void)ptr; (void)len; errno = ENOSYS; return -1; }
int _open(const char* name, int flags, int mode) { (void)name; (void)flags; (void)mode; errno = ENOSYS; return -1; }
int _close(int file)                      { (void)file; return -1; }
int _lseek(int file, int ptr, int dir)    { (void)file; (void)ptr; (void)dir; return 0; }
int _isatty(int file)                     { (void)file; return 1; }
int _getpid(void)                         { return 1; }
int _kill(int pid, int sig)               { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getentropy(void* buf, size_t len)    { (void)buf; (void)len; errno = ENOSYS; return -1; }

int _fstat(int file, struct stat* st)
{
    (void)file;
    st->st_mode = S_IFCHR;   /* character device: stdout stays line-buffered-small */
    return 0;
}
