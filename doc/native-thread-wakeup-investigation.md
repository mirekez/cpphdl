# Native graph wakeup investigation, 2026-11-11

The Sirius Llama simulation stopped making progress with its caller waiting in
`Threads::runTasks` during memory commit and all 29 workers waiting in
`Threads::worker`. The process survived the Quartus OOM event. Attaching GDB was
followed by renewed progress. We did not capture the command/completion stamp
values while it was stuck, so these observations do not establish the exact
cause of that stall.

## Ordering hazard and workaround

The completion path stored a stamp with release ordering and immediately called
`notify_all()`. Older libstdc++ implementations check a shared waiter count before
making the futex wake syscall. A release store followed by a sequentially
consistent load of a *different* atomic does not provide a StoreLoad barrier.
Consequently the notifier can observe zero waiters while a newly registered
waiter still observes the old stamp. The kernel's subsequent value check often
prevents sleeping, but it cannot help if the stamp is still invisible then.

This is the mechanism described in
[GCC PR127757](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=127757) and its
[upstream fix](https://github.com/gcc-mirror/gcc/commit/05ac72feea409f96c2b690a6f064171e274c8f91).
GCC 11–15 have the same ordering pattern, but register the waiter before a spin
loop, making the actual lost wake much harder to trigger. Sirius's supplied
disassembly shows the release store and waiter-count check without an intervening
barrier. Recovery after a debugger interrupts a futex wait is consistent with
this mechanism; it is not sufficient to prove causation.

The workaround is **disabled by default**. Define `FIX_SYNCHRO_STALLING_LOCK`
when compiling the simulation to publish command, completion, dependency and
shutdown stamps with sequentially consistent ordering. Without this macro,
`cpphdl_graph_threads.h` retains the original release ordering and its performance.
Use the same definition in every translation unit including this header, including
generated graph sources and the harness. For example, add
`-DFIX_SYNCHRO_STALLING_LOCK` to their C++ compilation flags and rebuild them.

When enabled, this supplies the missing
ordering before the library checks the waiter count. It retains the existing
wait protocol, task scheduling, and hardware simulation semantics. This is a
workaround for affected standard libraries, not a requirement imposed by the
C++ atomic wait/notify specification. It does not introduce periodic wakeups or
pretend that an unfinished task has completed.

The already-running Sirius executable has **not** acquired this change. Enabling
the workaround requires a rebuild; the recovered run need not be discarded.

## Local experiments

Tests ran on this host's six available CPUs, without Quartus, memory exhaustion,
or changes to the Sirius process. GCC 11 experiments overlaid the unmodified
GCC 11.5 `bits/atomic_wait.h` on the system toolchain, not an entire GCC 11
installation. The Clang dispatcher tests used Clang 21 with system startup
objects and that header overlay.

| Experiment | Result |
| --- | --- |
| Memory-order diagnostic, three trials of one million iterations per publication policy | Release: 1,479 / 378 / 677 both-stale outcomes; seq_cst: 0 / 0 / 0 |
| Original dispatcher, 30 workers, spin limit 0, 120 seconds | 1,178,542 variable-task dispatch rounds; no hang or result mismatch |
| Original dispatcher, 6 workers, spin limit 4096, 120 seconds | 5,666,768 rounds; no hang or result mismatch |
| Ordered dispatcher, 30 workers, spin limit 0, 120 seconds | 1,862,656 rounds; no hang or result mismatch |
| Six-worker dispatcher with 300 SIGSTOP/SIGCONT pairs per run, 60 seconds, spin limit 0 | Original: 2,691,034 rounds; ordered: 1,809,561 rounds; both passed. Other validation ran concurrently, so these counts are not a speed comparison. |
| Upstream atomic wait/notify reproducer with GCC 11 header | Five million iterations, no hang |
| Same reproducer with the GCC 11 library spin loop disabled in a scratch header | Five million iterations, no hang; this was an amplified experiment, not a production configuration |
| Same reproducer with the installed GCC 16 library | Two million iterations, no hang |
| Runtime regression under ThreadSanitizer, before and after | Passed, no reported data races |
| Extended runtime regression with ordered publication | Passed at spin limits 0, 64 and 4096, including cold stores, dependencies, shared callers, exception draining, and worker shutdown |
| Wait-policy and dispatch-profiler regressions | Passed |
| Generated parallel RAM graph, active commits, task/memory worker caps 2 and 6 | Passed all 12 checks: snapshots, wide-write priority, clocks/reset, rejection/retry, concurrent copies |
| Generated native batch regression | Passed: 11 graphs, 11,264 model phases, reset/copies, static/dynamic scheduling, simultaneous jobs, exception draining, concurrent callers |

The ordering diagnostic counts a vulnerable decision, **not an actual blocked
kernel waiter**. None of these experiments reproduced the complete Sirius
stall. The workaround closes the demonstrated ordering window, but long-run
validation on Sirius is still needed before attributing its incident to this
cause.

The runtime regression now includes cold stores before both dependency and
worker-completion publication. CMake registers both default and
`FIX_SYNCHRO_STALLING_LOCK` variants at spin limits 0, 64 and 1024.
The standalone ordering diagnostic is also
registered as `cpp_graph_native_notify_ordering`. To run it separately from the
cpphdl repository root:

```bash
g++ -std=c++20 -O2 -pthread tests/graph/NativeNotifyOrdering.cc -o /tmp/native-notify-ordering
/tmp/native-notify-ordering 1000000
```

The release count is timing-dependent and may legitimately be zero. The
seq_cst count must always be zero.

The main CMake build compiled all three affected test targets, but its configured
Conda startup objects require x86-64-v3, which this virtual host does not fully
expose. All nine selected CTest cases therefore failed at loader startup with
`CPU ISA level is lower than required`, before entering test code. Functional
validation above used separately compiled system-GCC executables (and Clang
with system startup objects). Worker caps 1, 2, 4 and 30 were also checked with
the system-GCC runtime test. The existing project toolchain configuration was
not changed to hide this distinction.

## Dispatch cost

Five paired trials used the same tiny callbacks and compiler, six workers,
caller affinity, 4096 spins and one million dispatches per case. Each callback
performed 32 integer recurrence steps; every final checksum matched. Trials
alternated version order, with no other investigation workload running.

| Tasks per dispatch | Original median host seconds | Ordered median host seconds | Change |
| --- | ---: | ---: | ---: |
| 6 | 0.439051 | 0.484846 | +10.4% |
| 120 | 4.073634 | 4.281024 | +5.1% |

These are synchronization-heavy microbenchmarks, with substantial variation
between trials (including a 2.026462-second original six-task outlier). They do
not measure full Llama throughput or establish a corresponding full-model
slowdown. The ordering workaround has a measurable potential cost; it is not a
performance optimization.

Local scratch sources, original binaries, upstream reproducer, and logs are
under `hlsgen/fpga/reports/native-wait-investigation/` (ignored build artifacts).
