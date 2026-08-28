#include <stdint.h>

static constexpr uint32_t HARTS = 4;
static constexpr uintptr_t UART = 0x70000u;

alignas(64) static volatile uint32_t boot_count;
alignas(64) static volatile uint32_t barrier_count;
alignas(64) static volatile uint32_t barrier_phase;
alignas(64) static volatile uint32_t hart_seen[HARTS];
alignas(64) static volatile uint32_t atomic_counter;
alignas(64) static volatile uint32_t lr_word;
alignas(64) static volatile uint32_t lr_ready;
alignas(64) static volatile uint32_t lr_peer_done;
alignas(64) static volatile uint32_t lr_sc_failed;
alignas(2048) static volatile uint32_t eviction_lines[32][512];

static inline uint32_t amoadd(volatile uint32_t* address, uint32_t value)
{
    uint32_t old;
    __asm__ volatile("amoadd.w %0, %2, (%1)"
                     : "=r"(old) : "r"(address), "r"(value) : "memory");
    return old;
}

static inline uint32_t lr(volatile uint32_t* address)
{
    uint32_t value;
    __asm__ volatile("lr.w %0, (%1)" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static inline uint32_t sc(volatile uint32_t* address, uint32_t value)
{
    uint32_t status;
    __asm__ volatile("sc.w %0, %2, (%1)"
                     : "=r"(status) : "r"(address), "r"(value) : "memory");
    return status;
}

static void barrier(uint32_t phase)
{
    uint32_t old = amoadd(&barrier_count, 1);
    if (old == HARTS - 1) {
        barrier_count = 0;
        barrier_phase = phase;
    }
    else {
        while (barrier_phase < phase) {}
    }
}

static void putc(char ch)
{
    *(volatile uint32_t*)UART = (uint32_t)(uint8_t)ch;
}

static void puts(const char* text)
{
    while (*text) {
        putc(*text++);
    }
}

[[noreturn]] static void fail()
{
    puts("MULTICORE FAIL\n");
    for (;;) {}
}

extern "C" int main(uint32_t hart)
{
    if (hart >= HARTS) {
        fail();
    }
    hart_seen[hart] = 0x13570000u | hart;
    amoadd(&boot_count, 1);
    while (boot_count != HARTS) {}

    /* Globally serialized AMOs from all four private L1 caches. */
    for (uint32_t i = 0; i < 32; ++i) {
        amoadd(&atomic_counter, 1);
    }
    barrier(1);

    /* A peer store between LR and SC must destroy hart 0's reservation. */
    if (hart == 0) {
        lr_word = 7;
        (void)lr(&lr_word);
        lr_ready = 1;
        while (!lr_peer_done) {}
        lr_sc_failed = sc(&lr_word, 11);
    }
    else if (hart == 1) {
        while (!lr_ready) {}
        lr_word = 99;
        lr_peer_done = 1;
    }
    barrier(2);

    /* Repeated peer stores to lines sharing an L1 set exercise invalidation
       overflow without requiring an enormous bare-metal image. */
    for (uint32_t i = hart; i < 256; i += HARTS) {
        eviction_lines[i & 31u][0] = 0x80000000u | (hart << 16) | i;
    }
    barrier(3);

    if (hart == 0) {
        if (atomic_counter != HARTS * 32u || lr_sc_failed == 0 || lr_word != 99) {
            fail();
        }
        for (uint32_t i = 0; i < HARTS; ++i) {
            if (hart_seen[i] != (0x13570000u | i)) {
                fail();
            }
        }
        puts("MULTICORE PASS\n");
    }

    for (;;) {}
}
