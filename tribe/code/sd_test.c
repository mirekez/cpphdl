// SPDX-License-Identifier: MIT
#ifdef TRIBE_SD_TEST_TINY
/*
 * Tiny no-libc build used inside the SD image.  Loading a static libc binary
 * from this byte-polled PIO SD block driver takes long enough to hide real
 * driver bugs, so this mode keeps the executable small while testing the same
 * raw block read/write path.
 */

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned char uint8_t;
typedef unsigned int size_t;
typedef int ssize_t;

#define O_RDWR 2
#define SEEK_SET 0
#define AT_FDCWD (-100)
#define __NR_ioctl 29
#define __NR_openat 56
#define __NR_close 57
#define __NR_read 63
#define __NR_write 64
#define __NR_lseek 62
#define __NR_fsync 82
#define __NR_exit 93

static inline long sys_call0(long nr)
{
    register long a7 asm("a7") = nr;
    register long a0 asm("a0");
    asm volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long sys_call1(long nr, long x0)
{
    register long a7 asm("a7") = nr;
    register long a0 asm("a0") = x0;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long sys_call3(long nr, long x0, long x1, long x2)
{
    register long a7 asm("a7") = nr;
    register long a0 asm("a0") = x0;
    register long a1 asm("a1") = x1;
    register long a2 asm("a2") = x2;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}

static inline long sys_call4(long nr, long x0, long x1, long x2, long x3)
{
    register long a7 asm("a7") = nr;
    register long a0 asm("a0") = x0;
    register long a1 asm("a1") = x1;
    register long a2 asm("a2") = x2;
    register long a3 asm("a3") = x3;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
    return a0;
}

static inline long sys_call5(long nr, long x0, long x1, long x2, long x3, long x4)
{
    register long a7 asm("a7") = nr;
    register long a0 asm("a0") = x0;
    register long a1 asm("a1") = x1;
    register long a2 asm("a2") = x2;
    register long a3 asm("a3") = x3;
    register long a4 asm("a4") = x4;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a7) : "memory");
    return a0;
}

static long sys_openat(const char *path)
{
    return sys_call4(__NR_openat, AT_FDCWD, (long)path, O_RDWR, 0);
}

static void puts1(const char *s)
{
    const char *p = s;
    while (*p) {
        ++p;
    }
    sys_call3(__NR_write, 1, (long)s, p - s);
}

static void puthex8(uint32_t v)
{
    char s[11];
    int i;
    s[0] = '0';
    s[1] = 'x';
    for (i = 0; i < 8; ++i) {
        uint32_t n = (v >> (28 - i * 4)) & 15u;
        s[2 + i] = (char)(n < 10 ? '0' + n : 'a' + n - 10);
    }
    s[10] = 0;
    puts1(s);
}

static int streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static uint32_t parse_u32(const char *s)
{
    uint32_t v = 0;
    int hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    if (hex) {
        s += 2;
    }
    while (*s) {
        char c = *s++;
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        }
        else if (hex && c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        }
        else if (hex && c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        }
        else {
            break;
        }
        v = hex ? (v << 4) + d : v * 10u + d;
    }
    return v;
}

static uint64_t xorshift64(uint64_t x)
{
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}

static uint8_t prbs_byte(uint32_t byte_offset, uint32_t seed)
{
    uint64_t x = (uint64_t)byte_offset ^ ((uint64_t)seed << 32) ^ 0x9e3779b97f4a7c15ull;
    x = xorshift64(x);
    x += xorshift64(x + 0x6a09e667f3bcc909ull);
    return (uint8_t)x;
}

static uint8_t buffer[4096];

static int full_write_at(int fd, const void *buf, size_t len, uint32_t off)
{
    const uint8_t *ptr = (const uint8_t *)buf;
    uint64_t result = 0;
    if (sys_call5(__NR_lseek, fd, 0, off, (long)&result, SEEK_SET) < 0) {
        return -1;
    }
    while (len) {
        long n = sys_call3(__NR_write, fd, (long)ptr, len);
        if (n <= 0) {
            return -1;
        }
        ptr += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int full_read_at(int fd, void *buf, size_t len, uint32_t off)
{
    uint8_t *ptr = (uint8_t *)buf;
    uint64_t result = 0;
    if (sys_call5(__NR_lseek, fd, 0, off, (long)&result, SEEK_SET) < 0) {
        return -1;
    }
    while (len) {
        long n = sys_call3(__NR_read, fd, (long)ptr, len);
        if (n <= 0) {
            return -1;
        }
        ptr += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int tiny_main(int argc, char **argv)
{
    const char *device = "/dev/tribesd1";
    uint32_t offset = 16u * 1024u * 1024u;
    uint32_t block_size = 512;
    uint32_t max_bytes = 512;
    uint32_t seed = 0x1234abcd;
    int fd;
    uint32_t i;
    int argi;

    for (argi = 1; argi < argc; ++argi) {
        if (streq(argv[argi], "--offset-bytes") && argi + 1 < argc) {
            offset = parse_u32(argv[++argi]);
        }
        else if (streq(argv[argi], "--block-size") && argi + 1 < argc) {
            block_size = parse_u32(argv[++argi]);
        }
        else if (streq(argv[argi], "--max-bytes") && argi + 1 < argc) {
            max_bytes = parse_u32(argv[++argi]);
        }
        else if (streq(argv[argi], "--seed") && argi + 1 < argc) {
            seed = parse_u32(argv[++argi]);
        }
        else if (argv[argi][0] != '-') {
            device = argv[argi];
        }
    }
    if (block_size == 0 || block_size > sizeof(buffer) || (block_size & 511u) != 0) {
        puts1("SDTEST bad block size\n");
        return 2;
    }
    if (max_bytes == 0 || max_bytes > sizeof(buffer)) {
        max_bytes = block_size;
    }
    max_bytes -= max_bytes % block_size;
    if (max_bytes == 0) {
        max_bytes = block_size;
    }

    puts1("SDTEST tiny start offset=");
    puthex8(offset);
    puts1(" bytes=");
    puthex8(max_bytes);
    puts1("\n");

    fd = (int)sys_openat(device);
    if (fd < 0) {
        puts1("SDTEST open failed\n");
        return 1;
    }
    for (i = 0; i < max_bytes; ++i) {
        buffer[i] = prbs_byte(offset + i, seed);
    }
    if (full_write_at(fd, buffer, max_bytes, offset) != 0) {
        puts1("SDTEST write failed\n");
        return 1;
    }
    sys_call1(__NR_fsync, fd);
    for (i = 0; i < max_bytes; ++i) {
        buffer[i] = 0;
    }
    if (full_read_at(fd, buffer, max_bytes, offset) != 0) {
        puts1("SDTEST read failed\n");
        return 1;
    }
    for (i = 0; i < max_bytes; ++i) {
        uint8_t expected = prbs_byte(offset + i, seed);
        if (buffer[i] != expected) {
            puts1("SDTEST mismatch at ");
            puthex8(offset + i);
            puts1(" got=");
            puthex8(buffer[i]);
            puts1(" expected=");
            puthex8(expected);
            puts1("\n");
            return 1;
        }
    }
    sys_call1(__NR_close, fd);
    puts1("SDTEST PASSED\n");
    return 0;
}

__attribute__((used, noinline, noreturn)) void tiny_start_from_sp(unsigned long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)&sp[1];
    sys_call1(__NR_exit, tiny_main(argc, argv));
    for (;;) {
    }
}

__attribute__((naked, noreturn)) void _start(void)
{
    asm volatile(
        "mv a0, sp\n"
        "call tiny_start_from_sp\n"
        :
        :
        : "a0", "memory");
    for (;;) {
    }
}

#else
/*
 * Destructive raw block test for the Tribe SD Linux driver.
 *
 * The test writes deterministic PRBS data to /dev/tribesd1 in a full-device
 * quasi-random block order, then reads the same blocks back in another
 * quasi-random order and verifies the contents.  It intentionally bypasses the
 * filesystem and overwrites the target block device.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

static uint64_t xorshift64(uint64_t x)
{
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}

static uint32_t prbs_byte(uint64_t byte_offset, uint32_t seed)
{
    uint64_t x = byte_offset ^ ((uint64_t)seed << 32) ^ 0x9e3779b97f4a7c15ull;
    x = xorshift64(x);
    x += xorshift64(x + 0x6a09e667f3bcc909ull);
    return (uint32_t)x & 0xffu;
}

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b != 0) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static uint64_t choose_stride(uint64_t blocks, uint32_t seed)
{
    uint64_t stride = ((uint64_t)seed * 1103515245ull + 12345ull) | 1ull;
    if (blocks <= 1) {
        return 1;
    }
    stride %= blocks;
    if (stride == 0) {
        stride = 1;
    }
    while (gcd64(stride, blocks) != 1) {
        stride += 2;
        if (stride >= blocks) {
            stride = 1;
        }
    }
    return stride;
}

static double now_seconds(void)
{
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    }
#endif
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
    }
    return 0.0;
}

static int full_write_at(int fd, const void *buf, size_t len, uint64_t off)
{
    const uint8_t *ptr = (const uint8_t *)buf;
    if (lseek(fd, (off_t)off, SEEK_SET) < 0) {
        return -1;
    }
    while (len != 0) {
        ssize_t done = write(fd, ptr, len);
        if (done < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (done == 0) {
            errno = EIO;
            return -1;
        }
        ptr += done;
        len -= (size_t)done;
    }
    return 0;
}

static int full_read_at(int fd, void *buf, size_t len, uint64_t off)
{
    uint8_t *ptr = (uint8_t *)buf;
    if (lseek(fd, (off_t)off, SEEK_SET) < 0) {
        return -1;
    }
    while (len != 0) {
        ssize_t done = read(fd, ptr, len);
        if (done < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (done == 0) {
            errno = EIO;
            return -1;
        }
        ptr += done;
        len -= (size_t)done;
    }
    return 0;
}

static int get_device_size(int fd, uint64_t *size)
{
    uint64_t bytes = 0;
    unsigned long sectors = 0;
    off_t end;

    if (ioctl(fd, BLKGETSIZE64, &bytes) == 0 && bytes != 0) {
        *size = bytes;
        return 0;
    }
    if (ioctl(fd, BLKGETSIZE, &sectors) == 0 && sectors != 0) {
        *size = (uint64_t)sectors * 512ull;
        return 0;
    }
    end = lseek(fd, 0, SEEK_END);
    if (end > 0) {
        *size = (uint64_t)end;
        return 0;
    }
    return -1;
}

static void fill_block(uint8_t *buf, uint32_t block_size, uint64_t off, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < block_size; ++i) {
        buf[i] = (uint8_t)prbs_byte(off + i, seed);
    }
}

static int verify_block(const uint8_t *buf, uint32_t block_size, uint64_t off, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < block_size; ++i) {
        uint8_t expected = (uint8_t)prbs_byte(off + i, seed);
        if (buf[i] != expected) {
            fprintf(stderr,
                "verify mismatch at byte offset 0x%08" PRIx64
                " got=0x%02x expected=0x%02x\n",
                off + i, buf[i], expected);
            return -1;
        }
    }
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [device] [--block-size N] [--offset-bytes N] [--max-bytes N] [--seed N] [--passes N]\n"
        "\n"
        "Default device is /dev/tribesd1. This test is destructive.\n",
        argv0);
}

int main(int argc, char **argv)
{
    const char *device = "/dev/tribesd1";
    uint32_t block_size = 4096;
    uint32_t seed = 0x1234abcd;
    uint32_t passes = 1;
    uint64_t offset_bytes = 0;
    uint64_t max_bytes = 0;
    uint64_t size;
    uint64_t blocks;
    uint64_t block;
    uint64_t index;
    uint64_t stride;
    uint8_t *buf;
    double t0;
    double elapsed;
    int fd;
    int argi;

    for (argi = 1; argi < argc; ++argi) {
        if (strcmp(argv[argi], "--block-size") == 0 && argi + 1 < argc) {
            block_size = (uint32_t)strtoul(argv[++argi], NULL, 0);
        }
        else if (strcmp(argv[argi], "--max-bytes") == 0 && argi + 1 < argc) {
            max_bytes = strtoull(argv[++argi], NULL, 0);
        }
        else if (strcmp(argv[argi], "--offset-bytes") == 0 && argi + 1 < argc) {
            offset_bytes = strtoull(argv[++argi], NULL, 0);
        }
        else if (strcmp(argv[argi], "--seed") == 0 && argi + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++argi], NULL, 0);
        }
        else if (strcmp(argv[argi], "--passes") == 0 && argi + 1 < argc) {
            passes = (uint32_t)strtoul(argv[++argi], NULL, 0);
        }
        else if (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        else if (argv[argi][0] != '-') {
            device = argv[argi];
        }
        else {
            usage(argv[0]);
            return 2;
        }
    }

    if (block_size == 0 || (block_size & 511u) != 0) {
        fprintf(stderr, "block size must be a non-zero multiple of 512\n");
        return 2;
    }
    if (passes == 0) {
        passes = 1;
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror(device);
        return 1;
    }
    if (get_device_size(fd, &size) != 0) {
        perror("device size");
        close(fd);
        return 1;
    }
    if (offset_bytes >= size) {
        fprintf(stderr, "offset is outside the device\n");
        close(fd);
        return 1;
    }
    size -= offset_bytes;
    offset_bytes -= offset_bytes % block_size;
    if (max_bytes != 0 && max_bytes < size) {
        size = max_bytes;
    }
    size -= size % block_size;
    blocks = size / block_size;
    if (blocks == 0) {
        fprintf(stderr, "test size is smaller than block size\n");
        close(fd);
        return 1;
    }

    buf = (uint8_t *)malloc(block_size);
    if (!buf) {
        perror("malloc");
        close(fd);
        return 1;
    }

    printf("SDTEST device=%s offset=%" PRIu64 " size=%" PRIu64 " block=%u blocks=%" PRIu64
           " passes=%u seed=0x%08x\n",
           device, offset_bytes, size, block_size, blocks, passes, seed);
    printf("WARNING: writing raw PRBS data over the target device\n");

    for (uint32_t pass = 0; pass < passes; ++pass) {
        uint32_t pass_seed = seed + pass * 0x9e37u;

        stride = choose_stride(blocks, pass_seed);
        index = pass_seed % blocks;
        t0 = now_seconds();
        for (block = 0; block < blocks; ++block) {
            uint64_t off = index * block_size;
            fill_block(buf, block_size, offset_bytes + off, pass_seed);
            if (full_write_at(fd, buf, block_size, offset_bytes + off) != 0) {
                perror("write");
                free(buf);
                close(fd);
                return 1;
            }
            index += stride;
            index %= blocks;
        }
        fsync(fd);
        elapsed = now_seconds() - t0;
        printf("pass %u write: %.3f s, %.3f KiB/s\n", pass,
               elapsed, elapsed > 0.0 ? (double)size / 1024.0 / elapsed : 0.0);

        stride = choose_stride(blocks, pass_seed ^ 0xa5a55a5au);
        index = (pass_seed ^ 0x5a5aa5a5u) % blocks;
        t0 = now_seconds();
        for (block = 0; block < blocks; ++block) {
            uint64_t off = index * block_size;
            memset(buf, 0, block_size);
            if (full_read_at(fd, buf, block_size, offset_bytes + off) != 0) {
                perror("read");
                free(buf);
                close(fd);
                return 1;
            }
            if (verify_block(buf, block_size, offset_bytes + off, pass_seed) != 0) {
                free(buf);
                close(fd);
                return 1;
            }
            index += stride;
            index %= blocks;
        }
        elapsed = now_seconds() - t0;
        printf("pass %u read+verify: %.3f s, %.3f KiB/s\n", pass,
               elapsed, elapsed > 0.0 ? (double)size / 1024.0 / elapsed : 0.0);
    }

    printf("SDTEST PASSED\n");
    free(buf);
    close(fd);
    return 0;
}
#endif
