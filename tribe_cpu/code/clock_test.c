#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

struct kernel_timespec64 {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static void dump_bytes(const char *name, const void *ptr, size_t size)
{
    const unsigned char *p = (const unsigned char *)ptr;

    printf("%s bytes:", name);
    for (size_t i = 0; i < size; ++i) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

static void print_libc_clock(clockid_t clk, const char *name)
{
    struct timespec ts;
    int rc;

    memset(&ts, 0xa5, sizeof(ts));
    errno = 0;
    rc = clock_gettime(clk, &ts);
    printf("libc clock_gettime(%s): rc=%d errno=%d tv_sec=%lld tv_nsec=%lld sizeof(timespec)=%zu sizeof(time_t)=%zu\n",
        name, rc, errno, (long long)ts.tv_sec, (long long)ts.tv_nsec,
        sizeof(ts), sizeof(time_t));
    dump_bytes("  libc timespec", &ts, sizeof(ts));
}

static void print_syscall_clock(clockid_t clk, const char *name)
{
#ifdef SYS_clock_gettime
    struct timespec ts;
    long rc;

    memset(&ts, 0xa5, sizeof(ts));
    errno = 0;
    rc = syscall(SYS_clock_gettime, clk, &ts);
    printf("syscall SYS_clock_gettime(%s -> struct timespec): rc=%ld errno=%d tv_sec=%lld tv_nsec=%lld nr=%ld\n",
        name, rc, errno, (long long)ts.tv_sec, (long long)ts.tv_nsec,
        (long)SYS_clock_gettime);
    dump_bytes("  syscall timespec", &ts, sizeof(ts));
#else
    printf("SYS_clock_gettime is not defined\n");
#endif
}

static void print_syscall_clock64(clockid_t clk, const char *name)
{
#ifdef SYS_clock_gettime64
    struct kernel_timespec64 ts;
    long rc;

    memset(&ts, 0xa5, sizeof(ts));
    errno = 0;
    rc = syscall(SYS_clock_gettime64, clk, &ts);
    printf("syscall SYS_clock_gettime64(%s -> kernel_timespec64): rc=%ld errno=%d tv_sec=%lld tv_nsec=%lld nr=%ld\n",
        name, rc, errno, (long long)ts.tv_sec, (long long)ts.tv_nsec,
        (long)SYS_clock_gettime64);
    dump_bytes("  syscall timespec64", &ts, sizeof(ts));
#else
    printf("SYS_clock_gettime64 is not defined\n");
#endif
}

static void print_timeofday(void)
{
    struct timeval tv;
    int rc;

    memset(&tv, 0xa5, sizeof(tv));
    errno = 0;
    rc = gettimeofday(&tv, NULL);
    printf("gettimeofday: rc=%d errno=%d tv_sec=%lld tv_usec=%lld sizeof(timeval)=%zu\n",
        rc, errno, (long long)tv.tv_sec, (long long)tv.tv_usec, sizeof(tv));
    dump_bytes("  timeval", &tv, sizeof(tv));
}

int main(void)
{
    printf("CLOCKTEST start sizeof(long)=%zu sizeof(time_t)=%zu sizeof(timespec)=%zu\n",
        sizeof(long), sizeof(time_t), sizeof(struct timespec));

    for (int i = 0; i < 8; ++i) {
        printf("CLOCKTEST iter=%d\n", i);
        print_libc_clock(CLOCK_REALTIME, "REALTIME");
        print_syscall_clock(CLOCK_REALTIME, "REALTIME");
        print_syscall_clock64(CLOCK_REALTIME, "REALTIME");
        print_libc_clock(CLOCK_MONOTONIC, "MONOTONIC");
        print_syscall_clock(CLOCK_MONOTONIC, "MONOTONIC");
        print_syscall_clock64(CLOCK_MONOTONIC, "MONOTONIC");
        print_timeofday();
        sleep(1);
    }

    printf("CLOCKTEST done\n");
    return 0;
}
