#include <stdint.h>
#include <stddef.h>

extern volatile uint64_t tohost;
extern volatile uint64_t fromhost;

extern int main(void);

static uint64_t magic_mem[8] __attribute__((aligned(64)));

void printstr(const char *s)
{
    size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }

    magic_mem[0] = 64;
    magic_mem[1] = 1;
    magic_mem[2] = (uint64_t)(uintptr_t)s;
    magic_mem[3] = len;
    __asm__ volatile("fence rw, rw" ::: "memory");

    tohost = (uintptr_t)magic_mem;
    while (fromhost == 0) {
    }
    fromhost = 0;
    __asm__ volatile("fence rw, rw" ::: "memory");
}

void __attribute__((noreturn)) exit(int code)
{
    tohost = ((((uint64_t)(uintptr_t)code) << 17) >> 16) | 1;
    __asm__ volatile("fence rw, rw" ::: "memory");
    while (1) {
    }
}

uintptr_t handle_trap(uintptr_t cause, uintptr_t epc, uintptr_t regs[32])
{
    (void)cause;
    (void)epc;
    (void)regs;
    exit(1337);
}

void _init(int cid, int nc)
{
    (void)cid;
    (void)nc;
    exit(main());
}
