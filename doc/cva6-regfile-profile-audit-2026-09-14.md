# Register-file replay: checked performance diagnosis

Implemented follow-up: [automatic nested packed-store lowering](cva6-nested-packed-store-fix-2026-09-14.md).
The experiments below describe the diagnosis before that production fix.

## Conclusion

The register-file slowdown is primarily **nested packed-store representation
and the resulting C++ optimization barrier**, not repeated combinational
evaluation. This is now supported by timing interventions, function counts,
sampling, assembly, and Clang's optimization remarks.

A scratch code-generation experiment replaces only the representation of the
decoder's nested bit assignments with the existing native bit-store helper.
It preserves the original loops, comparisons, enables, store order, register
updates, scheduler, and driver. It reduces runtime by **83.3%**. No LUT, handwritten
register file, new scheduler, or hardware-specific replacement was introduced.
The experiment is not yet an automatic transformation in either production tool.

## Final controlled comparison

Each time covers **2,313,200 cycles: 50 complete replays of the 46,264-cycle
matmul trace**. Five interleaved trials per variant, pinned to CPU 2, with no
concurrent compilation or other profiling from this investigation. Medians:

| Variant | Seconds | Relative to clean Verilator O2 |
| --- | ---: | ---: |
| Verilator, explicit clean Clang O2 flags | 0.208337 | 1.00x |
| Verilator, explicit clean Clang O3 flags | 0.209599 | 1.01x |
| Previously retained Verilator binary | 0.224458 | 1.08x |
| Unmodified cpphdl L1, Clang O2 | 1.772791 | 8.51x |
| Unmodified cpphdl L1, Clang O3 | 0.508668 | 2.44x |
| Native packed-store experiment, Clang O2 | **0.296126** | **1.42x** |

Against the retained native binary, the original gap is **7.90x**, reproducing
the previous 7.87x result. The representation experiment reduces that to 1.32x.
The stricter clean-flags comparison is 1.42x; it must not be called native parity.
All timed variants produce checksum `33cd39c93d03a8bc`.

The final five-trial ranges are 0.206135–0.211482 s for clean native O2,
1.761367–1.776147 s for original L1, and 0.294541–0.301231 s for native stores.
Build time, trace loading, and reference checking are excluded from these timers.
The machine is a shared KVM guest; CPU pinning does not eliminate host noise.

## What was actually measured

### 1. Repeated timing and controlled interventions

The first series gives the following results. Each percentage is elapsed-time
reduction versus its contemporaneous unmodified L1 median, not throughput gain.

| Intervention | Median seconds | Time reduction |
| --- | ---: | ---: |
| Unmodified L1 | 1.773815 | reference |
| Fresh rebuild of L1 | 1.769629 | 0.24% |
| Full-combs instead of L1 | 1.768241 | 0.31% |
| All generated sources and driver in one translation unit | 1.760648 | 0.74% |
| Cache the last root-to-scheduler-state lookup | 1.802401 | -1.61% |
| Add a single-bit fast branch to generic slice writeback | 1.708420 | 3.69% |
| Force-inline packed `bits()` and `updateParent()` | 1.193585 | 32.71% |
| Change only cpphdl compilation to Clang O3 | 0.503473 | 71.62% |
| Change only cpphdl compilation to GCC O2 | 0.621971 | 64.94% |
| Lower nested stores to the existing native bit helper | 0.297201 | **83.25%** |
| Native stores plus the two force-inline attributes | 0.309247 | 82.57% |

Follow-up tests checked the cause of the large O3 benefit:

| Intervention | Median seconds |
| --- | ---: |
| Unmodified O2 | 1.773767 |
| O2 plus `-funroll-loops` | 1.779631 |
| O3 | 0.510792 |
| O3 plus `-fno-unroll-loops` | 0.498641 |
| O3 without loop/SLP vectorization | 0.443784 |
| O2 force-inlining the assignment wrapper as well | 0.543346 |

Thus unrolling and SIMD are not prerequisites for this speedup. Disabling
vectorization improves this particular O3 case by 13.1%, but is not evidence
for disabling vectorization across CVA6. The unity build is an integration
visibility experiment, not a test of every possible LTO configuration.

All changes are isolated under the audit build directory. In particular, the
sub-10% experiments and combinations that worsen their parent are **not applied
to production**. Successful interventions are also diagnostic only in this task.

### 2. Function and proxy-operation counts

An instrumented build executes one validation replay and one timed replay:
92,528 logical cycles plus 20 extra asynchronous-reset settling epochs.

- Decoder, read evaluator, sequential work, and commit: **92,548 entries each**.
- Exactly **one decoder evaluation per evaluation epoch**, not hundreds.
- Decoder single-bit writebacks: **5,923,072 = 64 x 92,548**.
- Logical slice constructions: 6,108,168 at parent width 10;
  17,994,382 at width 64; 412,742 at width 1024.

The instrumented source therefore requests about **265 slice constructions per
epoch**. These are logical C++ operations, not heap allocations or measured
retired instructions: instrumentation can prevent optimization. The separate
uninstrumented assembly confirms the important surviving hot-path calls.

The earlier approximately 521 evaluations per epoch belonged to the **complete
arbiter benchmark**, not this register file. Applying that diagnosis here would
have been wrong. Changing scheduler mode or caching its state lookup does not
solve this register-file case.

### 3. PC sampling of both implementations

User-space SIGPROF sampling of 23,132,000-cycle runs collected 17,516 samples
for original cpphdl, 2,250 for native, and 2,996 for the native-store experiment.
Additional profiles cover O3 and forced inlining.

Original cpphdl self-time shares:

| Function/category | Samples |
| --- | ---: |
| Generated decoder body | 43.15% |
| Packed-array element `bits()` | 20.43% |
| 64-bit-parent slice writeback | 14.92% |
| Sequential work body | 8.51% |
| 1024-bit-parent slice writeback | 2.02% |
| Root-to-state hash lookup | 1.80% |
| `calc_all` itself | 0.55% |

These are approximate whole-process PC self-time shares, not exclusive
timed-loop measurements or caller-attributed costs. Validation is only one
replay versus 500 timed replays. Signals can coalesce; the profiles are not
hardware instruction counts. About 5.94% of original cpphdl samples lie outside
mapped executable functions. Do not label generated-body time entirely as
scheduler overhead.

After native-store lowering, the packed `bits()` hotspot disappears. Sequential
work becomes 32.28%, decoder 20.89%, and the remaining 64/1024-bit slice
writebacks together 21.23%. Native sampling is dominated by its generated
combinational and sequential bodies, not cpphdl-style slice helpers.

Hardware `perf stat` was attempted and denied (`perf_event_paranoid=4`); Valgrind
is not installed. No cache-miss, branch-miss, or instruction-count claim is made.

### 4. Assembly and compiler optimization remarks

The original O2 decoder makes **two out-of-line helper calls for each of its
64 bit stores**: construct the selected bit view, then write it back. Those
helpers retain general range handling, parent pointers, temporary initialization,
and copying. The decoder also reloads its input address inside the loop.

Clang explicitly reports why the calls survive:

- Packed `bits()` inline cost 345; O2 callsite threshold 225: rejected.
- `updateParent()` cost 360; O2 threshold 225: rejected.
- At O3, those callsites have threshold 525: both accepted.

This verifies the compiler-sensitivity explanation rather than merely inferring
it from timings. However, O3 still emits substantial general slice logic.
Its decoder body is 1,708 bytes, versus 465 bytes plus helpers at O2.
Forcing only two helpers inline moves writeback into an out-of-line assignment
wrapper; the forced-inline profile finds that wrapper consuming 25.45%.

In the native-store experiment the decoder has **no helper calls**, loads the
input addresses before its loops, keeps the decoder word in a register, and
stores it once at the end. Its body is 296 bytes. This is the native C++ compiler
optimizing transparent stores; the experiment did not replace the decoder with
a handwritten one-hot formula or delete the original loops.

The production lowering at `Combs.cpp:3399` recognizes a direct field and one
index, excludes indexed storage, and expects assignment after that index.
Consequently it handles the old request-tree scalar-bit case but misses
`we_dec_comb[port][bit]`. The same missed shape exists in the full conversion.

## Assumptions checked and limitations

- The fresh O2 cpphdl rebuild is **byte-identical** to the retained L1 executable:
  SHA-256 `0e800e1e5494ada03b3495bb56fed77685006e263f3e9fe2f1a7d5077ad0d32a`.
- Every timing run checks all 46,264 captured read pairs before timing. Random
  runs additionally compare 20,000 outputs and a 200,000-cycle checksum.
- An independent reference model generates 8,193 directed records, covering all
  4,096 combinations of two write addresses and enables, with subsequent reads.
  This includes disabled writes, x0, register 31, and both-port collision
  priority. All **19 tested binaries** pass those records and two-replay checksums.
- Native inherited Conda flags were inspected. An attempted make-only O3
  override was found to be overridden later by O2 for generated/runtime objects;
  that artifact is retained as `verilator_O3_mixed`, not treated as a true O3
  control. The final native O2/O3 controls compile driver, model, and runtime
  directly with explicit flags, avoiding that ordering issue.
- One profiling attempt hit a sampler startup race: preloading `taskset` armed
  a timer that survived `exec` while its signal handler did not. The launcher now
  preloads only the model. The failed run contributed no samples and was rerun.
- No production compiler/runtime files were changed by this audit; all 27
  captured source/runtime/trace input hashes remain unchanged.

This establishes a causal reproducer for **one shared representation problem**,
not a calibrated model of all full-CVA6 costs. The full design additionally has
wide transfers, different hot blocks, and repeated producers. It was not rerun
in this audit. The register-file gain must not be extrapolated to an 83% full-core
gain or used to claim Verilator parity for CVA6.

## Implementation direction justified by these tests

Extend automatic packed-lvalue lowering to nested packed array/bit selections,
using type-derived offsets and widths, with existing evaluation order and
register commit semantics preserved. Do this in the conversion/emission path,
not as a special case for CVA6 or a new scheduler. Keep wide and non-packed
fallbacks correct. Regenerate the directed/random/trace tests, then the complete
CVA6 matmul comparison, before accepting a production optimization.

## Artifacts and local reproduction

Directory: `build/regfile-profile-audit-20260914/`.

- `experiments.py`: scratch variants, interleaved timing, profiles and counts.
- `native_clean.py`: explicit native O2/O3 builds.
- `directed.py`: independent collision/x0 test generation and validation.
- `summary.json`, `followup-summary.json`, `clean-flags-summary.json`: medians.
- Corresponding `*-timings.json`: every trial, command, checksum and output.
- `profiles.json`, `invocations.json`, `*-assembly.txt`, `inline-remarks*.log`:
  mechanism evidence; `build-commands.json` records experimental builds.

```sh
python3 build/regfile-profile-audit-20260914/experiments.py build
python3 build/regfile-profile-audit-20260914/native_clean.py
python3 build/regfile-profile-audit-20260914/directed.py
python3 build/regfile-profile-audit-20260914/experiments.py timing --trials 5 \
  --prefix clean-flags- --only verilator native_clean_O2 native_clean_O3 original_l1 O3 native_stores
python3 build/regfile-profile-audit-20260914/experiments.py profile
```

The scripts reuse the retained conversion and local tool paths; they are audit
artifacts, not a portable replacement for the maintained benchmark runner.
