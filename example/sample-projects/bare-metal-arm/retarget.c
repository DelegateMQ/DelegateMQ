#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <wchar.h>

#define UNUSED(x) (void)(x)
int errno;

/* Helper: Write string using Opcode 0x04 */
static void semihost_write0(const char *str) {
    register int r0 asm("r0") = 0x04;
    register void *r1 asm("r1") = (void*)str;
    __asm volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
}

/* Custom _write: Chunks data to avoid buffer limits */
int _write(int file, char *ptr, int len) {
    UNUSED(file);
    char buf[65]; 
    int i = 0;
    while (i < len) {
        int chunk_size = 0;
        while (chunk_size < 64 && (i + chunk_size) < len) {
            buf[chunk_size] = ptr[i + chunk_size];
            chunk_size++;
        }
        buf[chunk_size] = '\0';
        semihost_write0(buf);
        i += chunk_size;
    }
    return len;
}

/* Stubs */
void __malloc_lock(struct _reent *r)   { UNUSED(r); }
void __malloc_unlock(struct _reent *r) { UNUSED(r); }

extern char _end;
void *_sbrk(int incr) {
    static char *heap_end = 0;
    char *prev_heap_end;
    if (heap_end == 0) heap_end = &_end;
    prev_heap_end = heap_end;
    heap_end += incr;
    return (void *)prev_heap_end;
}

#undef putwc
#undef fputwc
#undef getwc
#undef fgetwc
#undef ungetwc
#undef swprintf
int swprintf(wchar_t *wcs, size_t maxlen, const wchar_t *format, ...) { UNUSED(wcs); UNUSED(maxlen); UNUSED(format); return 0; }
wint_t putwc(wchar_t c, FILE *stream) { UNUSED(c); UNUSED(stream); return (wint_t)c; }
wint_t fputwc(wchar_t c, FILE *stream) { return putwc(c, stream); }
wint_t getwc(FILE *stream) { UNUSED(stream); return WEOF; }
wint_t fgetwc(FILE *stream) { return getwc(stream); }
wint_t ungetwc(wint_t c, FILE *stream) { UNUSED(c); UNUSED(stream); return WEOF; }

int _open(const char *path, int flags, ...) { UNUSED(path); UNUSED(flags); return -1; }
int _close(int file) { UNUSED(file); return -1; }
int _fstat(int file, struct stat *st) { UNUSED(file); st->st_mode = S_IFCHR; return 0; }
int _isatty(int file) { UNUSED(file); return 1; }
int _lseek(int file, int ptr, int dir) { UNUSED(file); UNUSED(ptr); UNUSED(dir); return 0; }
int _read(int file, char *ptr, int len) { UNUSED(file); UNUSED(ptr); UNUSED(len); return 0; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { UNUSED(pid); UNUSED(sig); return -1; }
void _exit(int status) { UNUSED(status); while (1); }

/* newlib 4.4+: libstdc++'s std::random_device references getentropy(); no entropy source here */
int _getentropy(void *buf, size_t len) { UNUSED(buf); UNUSED(len); errno = ENOSYS; return -1; }