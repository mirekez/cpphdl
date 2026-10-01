#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#ifdef USE_CALLGRIND
#include <valgrind/callgrind.h>
#endif

/* Profile the existing binary, without recompilation or adding per-cycle work.
 * BusRun's main makes two steady_clock calls: validation is before the first.
 * Verilator also reads clocks during initialization, so filter by caller range.
 * Reject a changed timer contract rather than silently profiling the checker.
 * This interposer requires Linux x86-64 and the libstdc++ steady_clock ABI. */
static uintptr_t samples[1000000];
static volatile sig_atomic_t sample_count;
static volatile sig_atomic_t dropped_samples;
static unsigned monotonic_calls;
static unsigned ignored_calls;
static uintptr_t main_start;
static uintptr_t main_end;
static int enabled;
static int callgrind_mode;
static struct timespec cpu_start;
static struct timespec cpu_end;
static int (*original_clock_gettime)(clockid_t, struct timespec *);
static int64_t (*original_steady_now)(void);
static const char *output_prefix;
static char executable[4096];

static FILE *output_file(const char *suffix) {
    char path[8192];
    const int length = snprintf(path, sizeof(path), "%s.%s", output_prefix, suffix);
    if (length < 0 || (size_t)length >= sizeof(path)) abort();
    FILE *output = fopen(path, "w");
    if (!output) abort();
    return output;
}

static int write_module(struct dl_phdr_info *info, size_t size, void *opaque) {
    (void)size;
    FILE *output = opaque;
    if (!*info->dlpi_name) {
        main_start += info->dlpi_addr;
        main_end += info->dlpi_addr;
    }
    for (unsigned index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr) *segment = &info->dlpi_phdr[index];
        if (segment->p_type == PT_LOAD && (segment->p_flags & PF_X))
            fprintf(output, "%lx %lx %lx %s\n", (unsigned long)info->dlpi_addr,
                    (unsigned long)(info->dlpi_addr + segment->p_vaddr),
                    (unsigned long)(info->dlpi_addr + segment->p_vaddr + segment->p_memsz),
                    *info->dlpi_name ? info->dlpi_name : executable);
    }
    return 0;
}

static void sample_pc(int signal_number, siginfo_t *info, void *context) {
    (void)signal_number;
    (void)info;
    const ucontext_t *interrupted = context;
    if ((size_t)sample_count < sizeof(samples) / sizeof(samples[0]))
        samples[sample_count++] = interrupted->uc_mcontext.gregs[REG_RIP];
    else
        ++dropped_samples;
}

__attribute__((constructor)) static void initialize(void) {
    original_clock_gettime = dlsym(RTLD_NEXT, "clock_gettime");
    if (!original_clock_gettime) abort();
    const char *target = getenv("WORK_PROFILE_BINARY");
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0) abort();
    executable[length] = 0;
    /* LD_PRELOAD also reaches the Valgrind launcher; never instrument it. */
    if (!target || strcmp(executable, target)) return;
    original_steady_now = dlsym(RTLD_NEXT, "_ZNSt6chrono3_V212steady_clock3nowEv");
    if (!original_steady_now) abort();
    output_prefix = getenv("WORK_PROFILE_OUTPUT");
    if (!output_prefix) abort();
    const char *start = getenv("WORK_PROFILE_MAIN_START");
    const char *end = getenv("WORK_PROFILE_MAIN_END");
    if (!start || !end) abort();
    main_start = strtoull(start, NULL, 16);
    main_end = strtoull(end, NULL, 16);
    if (main_end <= main_start) abort();
    const char *mode = getenv("WORK_PROFILE_MODE");
    callgrind_mode = mode && !strcmp(mode, "callgrind");
#ifndef USE_CALLGRIND
    if (callgrind_mode) abort();
#endif
    FILE *modules = output_file("modules");
    dl_iterate_phdr(write_module, modules);
    if (fclose(modules)) abort();
    if (!callgrind_mode) {
        struct sigaction action = {0};
        action.sa_sigaction = sample_pc;
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGPROF, &action, NULL)) abort();
    }
    enabled = 1;
}

int64_t profile_steady_now(void) __asm__("_ZNSt6chrono3_V212steady_clock3nowEv");
int64_t profile_steady_now(void) {
    if (!original_steady_now) {
        original_steady_now = dlsym(RTLD_NEXT, "_ZNSt6chrono3_V212steady_clock3nowEv");
        if (!original_steady_now) abort();
    }
    const int64_t result = original_steady_now();
    if (!enabled) return result;
    const uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (caller < main_start || caller >= main_end) {
        ++ignored_calls;
        return result;
    }
    if (++monotonic_calls > 2) abort();
    if (monotonic_calls == 1) {
        original_clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_start);
        if (callgrind_mode) {
#ifdef USE_CALLGRIND
            /* --instr-atstart=no already excludes initialization. */
            CALLGRIND_START_INSTRUMENTATION;
#endif
        } else {
            struct itimerval timer = {0};
            const char *interval = getenv("WORK_PROFILE_INTERVAL_US");
            const long microseconds = interval ? strtol(interval, NULL, 10) : 1000;
            if (microseconds < 1000 || microseconds > 999999) abort();
            timer.it_interval.tv_usec = microseconds;
            timer.it_value = timer.it_interval;
            if (setitimer(ITIMER_PROF, &timer, NULL)) abort();
        }
    } else {
        if (callgrind_mode) {
#ifdef USE_CALLGRIND
            /* Stop clears global counters. Dump first so the call graph and
             * its summary describe the same completed work interval. */
            CALLGRIND_DUMP_STATS;
            CALLGRIND_STOP_INSTRUMENTATION;
#endif
        } else {
            struct itimerval timer = {0};
            if (setitimer(ITIMER_PROF, &timer, NULL)) abort();
        }
        original_clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end);
    }
    return result;
}

__attribute__((destructor)) static void finish(void) {
    if (!enabled) return;
    if (monotonic_calls != 2 || dropped_samples) abort();
    FILE *output = output_file("pcs");
    if (fwrite(samples, sizeof(samples[0]), sample_count, output) != (size_t)sample_count) abort();
    if (fclose(output)) abort();
    output = output_file("json");
    const double seconds = cpu_end.tv_sec - cpu_start.tv_sec + (cpu_end.tv_nsec - cpu_start.tv_nsec) * 1e-9;
    fprintf(output, "{\"samples\":%d,\"dropped\":%d,\"monotonic_calls\":%u,\"ignored_calls\":%u,\"cpu_seconds\":%.9f}\n",
            sample_count, dropped_samples, monotonic_calls, ignored_calls, seconds);
    if (fclose(output)) abort();
}
