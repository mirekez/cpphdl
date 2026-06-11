#include "uart.h"
#include <stdint.h>

#define PERF_WORDS 4096u
#define PERF_ROUNDS 1u
#define PERF_FUNCS 64u
#define PERF_TEXT_ROUNDS 192u

#define PTE_V (1u << 0)
#define PTE_R (1u << 1)
#define PTE_W (1u << 2)
#define PTE_X (1u << 3)
#define PTE_A (1u << 6)
#define PTE_D (1u << 7)

static uint32_t root_pt[1024] __attribute__((aligned(4096)));
static uint32_t leaf_pt[1024] __attribute__((aligned(4096)));
static uint32_t data_a[PERF_WORDS] __attribute__((aligned(64)));
static uint32_t data_b[PERF_WORDS] __attribute__((aligned(64)));
static volatile uint32_t perf_sink;

static inline void write_satp(uint32_t value)
{
    __asm__ volatile("csrw satp, %0" : : "r"(value) : "memory");
}

static inline void write_stvec(uint32_t value)
{
    __asm__ volatile("csrw stvec, %0" : : "r"(value) : "memory");
}

static inline void sfence_vma(void)
{
    __asm__ volatile(".word 0x12000073" : : : "memory");
}

static inline uint32_t read_cycle(void)
{
    uint32_t value;
    __asm__ volatile("rdcycle %0" : "=r"(value));
    return value;
}

static inline uint32_t read_scause(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, scause" : "=r"(value));
    return value;
}

static inline uint32_t read_stval(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, stval" : "=r"(value));
    return value;
}

static inline uint32_t read_sepc(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, sepc" : "=r"(value));
    return value;
}

static inline uint32_t pte(uint32_t ppn, uint32_t flags)
{
    return (ppn << 10) | flags;
}

static void unexpected_trap(void) __attribute__((noreturn, aligned(4)));
static void unexpected_trap(void)
{
    tribe_uart_puts("PERF_TRAP ");
    tribe_uart_put_i32((int32_t)read_scause());
    tribe_uart_putc(' ');
    tribe_uart_put_i32((int32_t)read_stval());
    tribe_uart_putc(' ');
    tribe_uart_put_i32((int32_t)read_sepc());
    tribe_uart_putc('\n');
    for (;;) {}
}

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

typedef uint32_t (*perf_func_t)(uint32_t);

#define PERF_TEXT_FUNC(N, A, B) \
    static uint32_t perf_text_func_##N(uint32_t x) __attribute__((noinline, aligned(1024))); \
    static uint32_t perf_text_func_##N(uint32_t x) \
    { \
        x = mix32(x + (uint32_t)(A)); \
        return x ^ data_a[(x + (uint32_t)(B)) & (PERF_WORDS - 1u)]; \
    }

PERF_TEXT_FUNC(0, 0x10203040u, 0x001u)
PERF_TEXT_FUNC(1, 0x31415926u, 0x083u)
PERF_TEXT_FUNC(2, 0x27182818u, 0x105u)
PERF_TEXT_FUNC(3, 0x9e3779b9u, 0x187u)
PERF_TEXT_FUNC(4, 0x7f4a7c15u, 0x209u)
PERF_TEXT_FUNC(5, 0x6a09e667u, 0x28bu)
PERF_TEXT_FUNC(6, 0xbb67ae85u, 0x30du)
PERF_TEXT_FUNC(7, 0x3c6ef372u, 0x38fu)
PERF_TEXT_FUNC(8, 0xa54ff53au, 0x411u)
PERF_TEXT_FUNC(9, 0x510e527fu, 0x493u)
PERF_TEXT_FUNC(10, 0x1f83d9abu, 0x515u)
PERF_TEXT_FUNC(11, 0x5be0cd19u, 0x597u)
PERF_TEXT_FUNC(12, 0xcbbb9d5du, 0x619u)
PERF_TEXT_FUNC(13, 0x629a292au, 0x69bu)
PERF_TEXT_FUNC(14, 0x9159015au, 0x71du)
PERF_TEXT_FUNC(15, 0x152fecd8u, 0x79fu)
PERF_TEXT_FUNC(16, 0x67332667u, 0x021u)
PERF_TEXT_FUNC(17, 0x8eb44a87u, 0x0a3u)
PERF_TEXT_FUNC(18, 0xdb0c2e0du, 0x125u)
PERF_TEXT_FUNC(19, 0x47b5481du, 0x1a7u)
PERF_TEXT_FUNC(20, 0x1db11bdau, 0x229u)
PERF_TEXT_FUNC(21, 0x5b5e0f6du, 0x2abu)
PERF_TEXT_FUNC(22, 0x2d9550abu, 0x32du)
PERF_TEXT_FUNC(23, 0x4f1bbcdcu, 0x3afu)
PERF_TEXT_FUNC(24, 0x0f6d2b69u, 0x431u)
PERF_TEXT_FUNC(25, 0x7c7d2d28u, 0x4b3u)
PERF_TEXT_FUNC(26, 0x165667b1u, 0x535u)
PERF_TEXT_FUNC(27, 0xd3a2646cu, 0x5b7u)
PERF_TEXT_FUNC(28, 0xfd7046c5u, 0x639u)
PERF_TEXT_FUNC(29, 0xb55a4f09u, 0x6bbu)
PERF_TEXT_FUNC(30, 0xb79f3ab5u, 0x73du)
PERF_TEXT_FUNC(31, 0x6c44198cu, 0x7bfu)
PERF_TEXT_FUNC(32, 0x4bdecfa9u, 0x041u)
PERF_TEXT_FUNC(33, 0xf6bb4b60u, 0x0c3u)
PERF_TEXT_FUNC(34, 0xbebfbc70u, 0x145u)
PERF_TEXT_FUNC(35, 0x289b7ec6u, 0x1c7u)
PERF_TEXT_FUNC(36, 0xeaa127fau, 0x249u)
PERF_TEXT_FUNC(37, 0xd4ef3085u, 0x2cbu)
PERF_TEXT_FUNC(38, 0x04881d05u, 0x34du)
PERF_TEXT_FUNC(39, 0xd9d4d039u, 0x3cfu)
PERF_TEXT_FUNC(40, 0xe6db99e5u, 0x451u)
PERF_TEXT_FUNC(41, 0x1fa27cf8u, 0x4d3u)
PERF_TEXT_FUNC(42, 0xc4ac5665u, 0x555u)
PERF_TEXT_FUNC(43, 0xf4292244u, 0x5d7u)
PERF_TEXT_FUNC(44, 0x432aff97u, 0x659u)
PERF_TEXT_FUNC(45, 0xab9423a7u, 0x6dbu)
PERF_TEXT_FUNC(46, 0xfc93a039u, 0x75du)
PERF_TEXT_FUNC(47, 0x655b59c3u, 0x7dfu)
PERF_TEXT_FUNC(48, 0x8f0ccc92u, 0x061u)
PERF_TEXT_FUNC(49, 0xffeff47du, 0x0e3u)
PERF_TEXT_FUNC(50, 0x85845dd1u, 0x165u)
PERF_TEXT_FUNC(51, 0x6fa87e4fu, 0x1e7u)
PERF_TEXT_FUNC(52, 0xfe2ce6e0u, 0x269u)
PERF_TEXT_FUNC(53, 0xa3014314u, 0x2ebu)
PERF_TEXT_FUNC(54, 0x4e0811a1u, 0x36du)
PERF_TEXT_FUNC(55, 0xf7537e82u, 0x3efu)
PERF_TEXT_FUNC(56, 0xbd3af235u, 0x471u)
PERF_TEXT_FUNC(57, 0x2ad7d2bbu, 0x4f3u)
PERF_TEXT_FUNC(58, 0xeb86d391u, 0x575u)
PERF_TEXT_FUNC(59, 0x67452301u, 0x5f7u)
PERF_TEXT_FUNC(60, 0xefcdab89u, 0x679u)
PERF_TEXT_FUNC(61, 0x98badcfeu, 0x6fbu)
PERF_TEXT_FUNC(62, 0x10325476u, 0x77du)
PERF_TEXT_FUNC(63, 0xc3d2e1f0u, 0x7ffu)

static const perf_func_t perf_text_funcs[PERF_FUNCS] = {
    perf_text_func_0, perf_text_func_1, perf_text_func_2, perf_text_func_3,
    perf_text_func_4, perf_text_func_5, perf_text_func_6, perf_text_func_7,
    perf_text_func_8, perf_text_func_9, perf_text_func_10, perf_text_func_11,
    perf_text_func_12, perf_text_func_13, perf_text_func_14, perf_text_func_15,
    perf_text_func_16, perf_text_func_17, perf_text_func_18, perf_text_func_19,
    perf_text_func_20, perf_text_func_21, perf_text_func_22, perf_text_func_23,
    perf_text_func_24, perf_text_func_25, perf_text_func_26, perf_text_func_27,
    perf_text_func_28, perf_text_func_29, perf_text_func_30, perf_text_func_31,
    perf_text_func_32, perf_text_func_33, perf_text_func_34, perf_text_func_35,
    perf_text_func_36, perf_text_func_37, perf_text_func_38, perf_text_func_39,
    perf_text_func_40, perf_text_func_41, perf_text_func_42, perf_text_func_43,
    perf_text_func_44, perf_text_func_45, perf_text_func_46, perf_text_func_47,
    perf_text_func_48, perf_text_func_49, perf_text_func_50, perf_text_func_51,
    perf_text_func_52, perf_text_func_53, perf_text_func_54, perf_text_func_55,
    perf_text_func_56, perf_text_func_57, perf_text_func_58, perf_text_func_59,
    perf_text_func_60, perf_text_func_61, perf_text_func_62, perf_text_func_63
};

static uint32_t phase_text_walk(uint32_t seed) __attribute__((noinline));
static uint32_t phase_text_walk(uint32_t seed)
{
    uint32_t acc = seed;
    // Linux boot spends most of its early time fetching a broad text working
    // set. Walk a 64 KiB noinline function footprint repeatedly so the 1 KiB
    // I-cache is forced into sustained refill stalls instead of one warm pass.
    for (uint32_t r = 0; r < PERF_TEXT_ROUNDS; ++r) {
        for (uint32_t i = 0; i < PERF_FUNCS * 2u; ++i) {
            uint32_t sel = (acc + i * 17u + r * 23u) & (PERF_FUNCS - 1u);
            acc = perf_text_funcs[sel](acc + i + (r << 8));
        }
    }
    return acc;
}

static uint32_t kernel_branch_mix(uint32_t x, uint32_t i) __attribute__((noinline));
static uint32_t kernel_branch_mix(uint32_t x, uint32_t i)
{
    switch ((x ^ i) & 7u) {
    case 0: x += (i << 3) ^ 0x10203040u; break;
    case 1: x = (x >> 1) ^ (i * 33u); break;
    case 2: x = (x << 5) + (x >> 7); break;
    case 3: x ^= 0xa5a55a5au + i; break;
    case 4: x += (x << 2) ^ 0x13579bdfu; break;
    case 5: x = (x >> 3) + (i << 9); break;
    case 6: x ^= (x << 11) | (i >> 2); break;
    default: x += mix32(i ^ x); break;
    }
    return mix32(x);
}

static uint32_t phase_stream_fill(void) __attribute__((noinline));
static uint32_t phase_stream_fill(void)
{
    uint32_t x = 0x12345678u;
    for (uint32_t i = 0; i < PERF_WORDS; ++i) {
        x = mix32(x + i);
        data_a[i] = x;
        data_b[i] = x ^ 0x5a5a5a5au;
    }
    return x;
}

static uint32_t phase_cache_walk(uint32_t seed) __attribute__((noinline));
static uint32_t phase_cache_walk(uint32_t seed)
{
    uint32_t acc = seed;
    uint32_t idx = seed & (PERF_WORDS - 1u);

    // This stride walks a working set larger than L1 and deliberately feeds
    // dependent addresses back into later loads, close to kernel list/tree code.
    for (uint32_t r = 0; r < PERF_ROUNDS; ++r) {
        for (uint32_t i = 0; i < PERF_WORDS; i += 8u) {
            idx = (idx + 2053u + (acc & 31u)) & (PERF_WORDS - 1u);
            acc += data_a[idx];
            data_b[(idx ^ 0x1555u) & (PERF_WORDS - 1u)] = acc ^ i;
            acc = kernel_branch_mix(acc, i + r);
        }
    }
    return acc;
}

static uint32_t phase_unaligned_masks(uint32_t seed) __attribute__((noinline));
static uint32_t phase_unaligned_masks(uint32_t seed)
{
    volatile uint8_t* bytes = (volatile uint8_t*)data_b;
    uint32_t acc = seed;

    // Byte and halfword stores exercise masked D-cache writes that Linux emits
    // around structure fields, command buffers, and network descriptors.
    for (uint32_t i = 1; i < 2048u; i += 3u) {
        bytes[i] = (uint8_t)(acc + i);
        bytes[i + 1u] ^= (uint8_t)(acc >> 8);
        acc += bytes[(i * 17u) & ((PERF_WORDS * 4u) - 1u)];
    }
    return acc;
}

static uint32_t phase_sv32_alias(uint32_t seed) __attribute__((noinline));
static uint32_t phase_sv32_alias(uint32_t seed)
{
    volatile uint32_t* alias;
    uint32_t acc = seed;

    root_pt[0] = pte(0, PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);
    root_pt[2] = pte(((uint32_t)leaf_pt) >> 12, PTE_V);
    leaf_pt[0] = pte(((uint32_t)data_a) >> 12, PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
    write_satp(0x80000000u | (((uint32_t)root_pt) >> 12));
    sfence_vma();

    // Access the same physical data through a non-identity Sv32 alias. This
    // keeps the MMU/TLB path in the measured region without needing Linux.
    alias = (volatile uint32_t*)(0x00800000u + (((uint32_t)data_a) & 0xfffu));
    for (uint32_t i = 0; i < 256u; ++i) {
        acc += alias[(i * 5u) & 511u];
        alias[(i * 13u) & 511u] = acc ^ i;
    }
    sfence_vma();
    return acc;
}

int main(void)
{
    uint32_t acc;

    write_stvec((uint32_t)unexpected_trap);
    write_satp(0);
    sfence_vma();

    acc = phase_stream_fill();
    acc ^= phase_text_walk(acc);
    acc ^= phase_cache_walk(acc);
    acc ^= phase_unaligned_masks(acc);
    acc ^= phase_sv32_alias(acc);
    write_satp(0);
    sfence_vma();
    acc ^= read_cycle();

    perf_sink = acc;
    if (perf_sink == 0x6a09e667u) {
        tribe_uart_puts("PERF_BAD\n");
        for (;;) {}
    }

    tribe_uart_puts("PERF\n");
    for (;;) {}
}
