#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "uart.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char __data_load_start;
extern char __data_start;
extern char __data_end;
extern char __bss_start;
extern char __bss_end;
extern char __heap_start;
extern char __heap_end;

void __tribe_init_memory(void)
{
    char* src = &__data_load_start;
    char* dst = &__data_start;
    while (dst < &__data_end) {
        *dst++ = *src++;
    }

    for (char* p = &__bss_start; p < &__bss_end; ++p) {
        *p = 0;
    }
}

int _write(int fd, const void* buf, size_t count)
{
    (void)fd;
    tribe_uart_write((const char*)buf, count);
    return (int)count;
}

void* _sbrk(ptrdiff_t increment)
{
    static char* heap;
    if (!heap) {
        heap = &__heap_start;
    }

    char* prev = heap;
    char* next = heap + increment;
    next = (char*)(((uintptr_t)next + 7u) & ~uintptr_t(7u));
    if (next < &__heap_start || next > &__heap_end) {
        errno = ENOMEM;
        return (void*)-1;
    }

    heap = next;
    return prev;
}

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat* st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd;
    (void)whence;
    return offset;
}

ssize_t _read(int fd, void* buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return 0;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}

#ifdef __cplusplus
}
#endif
