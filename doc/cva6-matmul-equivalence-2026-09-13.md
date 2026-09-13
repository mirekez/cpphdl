# Fresh CVA6 conversion: equivalent-work matmul check

Follow-up: `cva6-enhancement-investigation-2026-09-13.md` documents reproduced
field-dependency and array-copy defects and their compiler/runtime fixes. The
measurements here precede those fixes; the fixed full model is not yet validated.

## Result

**Fresh conversion and compilation completed, but functional equivalence did
not pass. There is no valid equivalent-output speed comparison yet.**

Both simulators execute the same integer-matmul ELF for exactly **69,315 total
clock edges: 10 reset clocks and 69,305 post-reset clocks**. Measured inside
the simulation, excluding initialization, reset, ELF loading, conversion, and
compilation:

| Simulator | Work clocks | Simulation work time | Process wall time | Result |
| --- | ---: | ---: | ---: | --- |
| Original CVA6 Verilator example | 69,305 | **4.514081 s** | 4.981635 s | `PASSED`, `tohost=0` |
| Fresh SV -> hdlcpp -> cpphdl L1 model | 69,305 | **50.528745 s** | 50.776445 s | Timeout; no `PASSED` |

These are **single validation samples, not accepted benchmark medians**. The
cycle counts match, but the outputs do not. Dividing these durations and calling
the result an equivalent-work speed ratio would be misleading. The comparison
script deliberately stops before its repeated timing trials and records
`equivalent: false`, without a speed ratio.

The ELF is `hdlcpp/tests/cva6/matrix_multiply.riscv`, SHA-256
`a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c`.
Its source checks integer matrix results 16, 24, 28, and 42 before printing
`PASSED`. Neither the program nor the hardware was replaced to obtain a result.

## What was rebuilt

Artifacts: `build/full-equivalence-20260912/` (the directory name records the
start date; the run completed on September 13).

- Fresh SystemVerilog conversion into a new `model/` directory, not a copy of
  the previous generated model. The maintained CVA6 conversion flow reports
  332 source entries, 284 conversion sources, 48 skipped entries, **287 generated
  files and zero conversion failures**. The flow still uses its pre-existing
  CVA6 compatibility maps; this is not a claim that those workarounds have been
  eliminated.
- Fresh hdlcpp specialization, then cpphdl L1 collection across 126 bounded
  units. Final graph: 958 instances, 611 scheduled values, 9,966 dynamic
  evaluators, 11,566 dynamic states, and 582 lazy-cycle back-edges.
- Fresh compilation of all 104 required object files, including constructors,
  runtime bridges, driver, and generated evaluators. No objects from the cached
  model were used. Clang 21.1.3, C++23, `-O2`, with the established constructor
  `-O0 -fno-inline` flags. The serial build was resumed with two jobs using the
  same flags and its already-built fresh objects/PCHs.
- The existing Verilator RTL model remains the reference. Its C++ testbench
  driver was rebuilt with measurement-only instrumentation; the original
  program still passes at 69,315 clocks with seed 1.
- The previous cached cpphdl runner remains intact at
  `hdlcpp/tests/cva6/cpphdl_testharness/run_cpphdl_testharness_opt`. The new runner
  is `build/full-equivalence-20260912/model/run_cpphdl_testharness_opt`.

Full conversion ran first with `CPPHDL_CVA6_SKIP_OPTIMIZE=1`, then continued
with `CPPHDL_CVA6_FINALIZE_ONLY=1` and `CPPHDL_COMB_OPTIMIZER_MODE=l1`. This split
allowed inspection of fresh conversion results before specialization; it did
not skip either optimization stage in the final model. See `conversion.log`,
`optimization.log`, `build.log`, `build-resumed.log`, and `resume_build.py`.

## Functional blocker

A separate, observation-enabled fresh cpphdl run records **zero RVFI
retirements through 5,000 clocks**. At cycle label 999, the frontend next-PC
probe reads `0x10004`, still in the boot-ROM region, rather than the matmul ELF
entry at `0x80000000`. The corresponding Verilator diagnostic records its first
retirement at cycle label 278, PC `0x10000`, and 181 retirement trace records
before the 1,000-clock cap. These diagnostics are outside the timed validation.

This establishes a functional/progress problem before matmul, not a valid
throughput difference on completed matmul work. It does **not** yet identify
the root cause within conversion, port handling, or scheduling. Correcting
that divergence is necessary before claiming an equivalent-output speedup.

Retirement observation is opt-in in the cpphdl driver. Its ordinary timeout
line's zero counter alone is not proof of zero retirement; the separate
`CPPHDL_OBSERVE_COMMITS=1` run provides the observation stated here. Logs are
`fresh-diagnostic.log` and `verilator-commits-1000.log`.

An attempted plain-hdlcpp diagnostic link could not resolve the separately
excluded root work/strobe instantiations; it is not used as a correctness or
performance result (`plain-build.log`).

## Reusable measurement

Both testbenches now support `CVA6_BENCH_STATS=1`, reporting reset, post-reset,
and total executed clocks plus `steady_clock` simulation-only elapsed time.
The cpphdl success message now counts the edge just executed, rather than
mistaking its zero-based cycle label for the number of completed edges.
These changes do not alter hardware scheduling or computation.

```sh
python3 hdlcpp/tests/cva6/compare_matmul.py \
  --verilator /home/me/cva6/work-ver/Variane_testharness \
  --cpphdl build/full-equivalence-20260912/model/run_cpphdl_testharness_opt \
  --output build/matmul-comparison --cpu 2
```

The comparator checks guest `PASSED`, successful `tohost=0` termination, and
identical, internally consistent clock counts. Only then does it run three
alternating timing trials and report medians/ratio. It rejects failed guests,
timeouts, missing statistics, and mismatched completion counts. Each run has
its own working directory. Inherited trace and fixed-cycle overrides are
cleared. ELF/binary hashes, commands, statuses, counts, and times are recorded.
Six regression tests for the validation gate pass; both repository diffs pass
`git diff --check`.

The authoritative result is
`build/full-equivalence-20260912/comparison/comparison.json`, with individual
logs beneath that directory. Detailed usage and fresh-build commands are in
`hdlcpp/tests/cva6/compare_matmul.md`. CPU affinity is set to CPU 2; no build
from this task runs during the final check. Unrelated user workloads were not
stopped, another reason not to treat these failed-validation samples as a
stable benchmark.

## Disk space

To accommodate a separate full build, four old diagnostic executables under
`build/architecture-20260912/` (`instrumented`, `optimized-helpers`,
`representations`, and `copies`) were losslessly archived as `.gz` files and
verified with `gzip -t` before removing the uncompressed copies. Their exact
bytes can be restored with `gzip -dk <file>.gz`. Logs, sources, and the old
production baseline were preserved.
The fresh build's disposable constructor PCH was removed after linking, as in
the normal build wrapper; the runnable model and generated source are retained.
