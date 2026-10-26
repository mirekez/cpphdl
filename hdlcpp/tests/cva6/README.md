# Reproducible full CVA6 comparison

These entry points use the full `cv32a6_imac_sv32` testharness, not a partial
replay. The default workload is the self-checking 32×32 integer matmul: 32,768
MACs, checksum `0xd687fdc5`, 396,778 total / 396,768 work cycles and 297,074
integer architectural commits with the reference source/toolchain.

## Prerequisites

Use an initialized CVA6 checkout (including its RTL submodules), built hdlcpp
and cpphdl tools, Clang with C++23 support, Verilator, and an RV32 bare-metal
toolchain plus Spike/FESVR libraries. The local defaults are:

| Environment override | Default |
| --- | --- |
| `CVA6_SRC` | `cva6` beside the cpphdl checkout |
| `CPPHDL` | `<cpphdl>/build/cpphdl` |
| `HDLCPP` | `<cpphdl>/hdlcpp/build/hdlcpp` |
| `CPPHDL_CXX` | `<cpphdl>/.conda/bin/clang++` |
| `VERILATOR` | `<CVA6_SRC>/tools/verilator-new/bin/verilator`, otherwise PATH |
| `RISCV` | `$HOME/riscv` |
| `CVA6_WORK_ROOT` | `/tmp/cpphdl-cva6-<uid>` |
| `ELF` | `<work-root>/elf/matrix_multiply.riscv` |

The tested original Verilator is 5.049; the older local 5.034 cannot build this
full model reliably. Rebuild your tools after changing compiler sources.
All artifacts, snapshots, objects and traces stay below `/tmp`. Source checkout
files are not modified. Builds are serial and guarded against low RAM, low
temporary space, oversized files and timeouts. Errors propagate to the caller.
The temporary-space guard accounts for Linux per-user quotas as well as `df`.

## Build and run

From this directory (or invoke the scripts by absolute path):

```sh
./.build_cva6.sh
./.build_cva6_hdlcpp.sh
./.compare.sh
```

`.build_cva6.sh` snapshots the original RTL/harness and builds the shared guest
ELF. `.build_cva6_hdlcpp.sh` regenerates ordinary C++ from that snapshot, without
running any simulation optimizer. `.compare.sh` builds the five execution
variants, validates them against original Verilator, and prints build times,
median simulation-work times, cycle counts and PASS/FAIL. It does **not** invoke
either preparation script or `.build_all.sh`. `.build_all.sh` performs both
preparation steps followed by all five builds.

Each build also has an independent run entry point:

| Build | Run | Meaning |
| --- | --- | --- |
| `.build_cva6_cpphdl.sh` | `.run_cva6_cpphdl.sh` | Default C++HDL runtime; no comb or native-graph optimizer |
| `.build_cva6_cpphdl-optimize-combs.sh` | `.run_cva6_cpphdl-optimize-combs.sh` | Only `--optimize-combs`; no L1, math or threaded optimization flags |
| `.build_cva6_cpphdl-native-graph.sh` | `.run_cva6_cpphdl-native-graph.sh` | Native graph, one lane/thread |
| `.build_cva6_cpphdl-native-graph-4threads.sh` | `.run_cva6_cpphdl-native-graph-4threads.sh` | Native graph, four lanes/threads |
| `.build_cva6_verilator.sh` | `.run_cva6_verilator.sh` | Original SystemVerilog through original Verilator harness, one thread |

For bounded C++ compilation, both non-graph C++ modes use hdlcpp's build-only
`--optimize` specialization/splitting pass. This does not replace their runtime
scheduler or enable cpphdl comb optimization in the default execution mode.
Constructor/assignment units retain hdlcpp's generated `-O0`/no-inline build
settings; the host runner, cycle units and generated comb kernels use `-O2`.
Compiler-derived type identities deduplicate explicit instantiations before
bounded unity compilation, avoiding repeated full-header parsing without
rewriting any model equations or allocating a multi-gigabyte PCH.
Optimized unity batches contain at most four source units (eight for constructor
units); compiler memory exhaustion retries smaller batches automatically.
Content-keyed partition checkpoints avoid repeating failed large batches when
an interrupted build resumes. Constructor units retain the stage's address-space
limit; multi-source constructor batches and optimized batches have a stricter
local limit and split before exhausting the host's available RAM.
Compiler subprocesses default to one glibc allocation arena, immediate heap
trimming, and a 64 KiB mmap threshold so freed large allocations can return to
the OS. Explicit allocator environment settings take precedence. These settings
do not alter compiler optimization flags, generated model semantics, or the
simulation runner's environment; the existing RAM safety guard remains enabled.
If one generated unit still exceeds that bound, partitioning continues at
explicit member-instantiation boundaries. Mixed singleton units separate
construction (`-O0`) from cycle methods (`-O2`), rather than optimizing large
constructor callback graphs together with simulation work. Pure explicit
instantiation units are partitioned before batching: constructors and `_assign`
use `-O0`, while `_work` and `_strobe` use `-O2`, even if the original file mixed
those phases. Binding methods run during setup, but install callbacks evaluated
during simulation. Keeping these units at `-O0` avoids the observed compiler
memory failure; callable code emitted alongside the bindings can also be
affected. This is a compilation-resource tradeoff, not a claim of unchanged
runtime performance. Compare execution times using the documented build policy.
Previously verified constructor batches stay separate from the new binding
batches so interrupted builds can reuse their object checkpoints.
Comb-only builds compile emitted comb partitions before the costly template
batches to surface backend errors early. Link order and optimization flags stay
unchanged; completed objects remain reusable if compilation is interrupted.
Completed object files are compressed and content-checked between compilation
and linking: concrete template names otherwise consume hundreds of megabytes
per object. Linking uses anonymous Linux memory files, then strips the final
symbol table. No model operations are replaced. Verified compressed objects
survive failed builds; successful builds remove their temporary object cache.
Comb collection checkpoints also survive interrupted builds. Their cache keys
cover model/runtime headers, collection sources, optimizer binary and collection
driver; only atomically completed, checksum-verified collections are reused.
Multi-alias collections use a local 4 GB address-space limit and split after
memory exhaustion; single aliases retain the stage's 6 GB limit. Completed
child collections and split checkpoints survive retries as well.
Non-opaque markers explicitly require complete template definitions with
`sizeof`: a pointer declaration alone need not instantiate the marked class,
which otherwise leaves missing children when bounded collections are merged.
The comb collection path avoids a multi-gigabyte PCH; ordinary C++ execution is
not silently replaced by the faster native graph backend.

All scripts accept `--help`, `--work-root PATH`, `--force`, `--timeout SECONDS`,
`--keep-build-files`, and `--keep-traces`. Build caching checks input and binary
hashes. The per-stage timeout defaults to four hours to accommodate ordinary
full-model template compilation on one build CPU. Run scripts reject stale
binaries, changed source snapshots, compiler binaries and FESVR inputs. Temporary compilation outputs
are removed after successful linking, and comparison traces are removed only
after complete commit validation. Compact JSON reports and logs remain.

`.compare.sh` defaults to one timed trial for each pair, CPU 2 for serial
execution (or the first allowed CPU if unavailable), and four allowed CPUs for
the four-thread candidate. Override with `--trials N`, `--cpu N`, `--cpus4 LIST`
or `--max-cycles N`. Additional trials alternate the execution order. For five
trials of the slow default runtime, use `--trials 5 --timeout 21600`: validation
plus five timed runs can exceed the normal four-hour stage timeout. One trial
matches the full comparison qualification below; it is not a multi-trial median.
Four-thread timings must not be pinned to a single CPU.
To compare only the original RTL, comb optimization, and one-thread native graph:

```sh
./.compare.sh --modes verilator cpphdl-optimize-combs cpphdl-native-graph
```

The default still covers all five backends. A selected comparison always includes
Verilator and retains the same output, cycle, and commit-stream equivalence checks.
The native build verifies the emitted lane count, rather than labeling a
silently reduced graph as four-threaded. DPI calls and their dependent cones
stay on the caller lane; independent pure cones execute in worker lanes.
Compilation/loading/initialization are excluded from simulation-work timing;
build and whole-process times are recorded separately. Failed validation never
produces a speed ratio. Compare requires matching checksum, reset/work/total
cycles and all ordered integer RVFI commits, plus actual native graph edges.
Build durations describe the successful invocation, which may reuse verified
objects or collection checkpoints from an earlier interrupted attempt; they are
not necessarily cold-build costs.

**Tracing caveat:** the original Verilator binary retains its architectural
trace formatting/output. C++ commit logging is enabled for validation only.
This intentionally repeats the accepted harness-level comparison, not a
trace-free engine benchmark. Exact wall times vary with host load; equivalence
and cycle counts, rather than a particular number of seconds, are enforced.

The small original ELF can be selected with `ELF="$PWD/matrix_multiply.riscv"`.
Use `python3 -B -m unittest discover -s . -p 'test_*.py'` for helper regressions;
set `CPPHDL_TEST_CXX` to enable the compiler-backed harness tests as well.
Set `CPPHDL_TEST_TOOL` to the cpphdl executable to exercise actual bounded AST
collection, including the incomplete-template regression.
Set `CPPHDL_TEST_HDLCPP` as well to verify concrete specialization preserves
the precedence of substituted non-type parameter expressions. Type parameters
remain type expressions; no numeric casts are introduced by this fix.

## Qualification (2026-10-22)

Fresh conversion emitted 287 headers with zero failures. The actual
`.compare.sh --trials 1` result, excluding compilation and initialization, is:

| Mode | Work seconds | Result |
| --- | ---: | --- |
| Original Verilator | 7.983710 | PASS |
| Default C++HDL runtime | 1089.619771 | PASS |
| Only `--optimize-combs` | — | BLOCKED: collection memory |
| Native graph, one thread | 4.859101 | PASS |
| Native graph, four threads | 3.185211 | PASS |

Every passing candidate matches the checksum, 396,778 total / 396,768 work
cycles and all 297,074 ordered integer commits. An additional five-trial native
qualification measured medians of 4.576255 seconds (one thread) and 3.097285
seconds (four threads); reference medians were 8.096051 and 8.004287 seconds.
These measurements retain the tracing caveat and constructor build policy
described above. All 60 helper regressions pass, including actual AST collection
and converter specialization.

## Follow-up qualification (2026-10-23)

Adaptive collection and complete template markers now allow full comb
collection to finish. The next blocker is cpphdl's comb parser rejecting
procedural initialization in `_assign()`, beginning with
`tandem_exit = logic<32>(0)` in the generated testharness. A minimal module
that initializes a value and then binds an input to it executes correctly
with the ordinary runtime but fails `--optimize-combs` with
`unsupported statement in _assign()`. These assignments must retain their
initialization semantics; removing them or substituting native graph execution
would not qualify the requested comb-only mode.

The phase-separated default build also compiled and linked successfully, but
a host reboot interrupted its full commit validation and cleared `/tmp`.
The table above remains the completed October 22 reference, not a result from
that interrupted run. All 60 helper regressions and both CTest checks
(`cva6_workflow_helpers`, `hdlcpp_specialization_precedence`) pass after reboot.

Fresh regeneration after reboot again emitted 287 headers with zero failures.
All three independent Verilator/native run scripts pass. Three alternating
timed trials after full integer-commit validation give:

| Candidate | Verilator median (s) | Candidate median (s) | Result |
| --- | ---: | ---: | --- |
| Native graph, one thread | 8.156320 | 4.788563 | PASS |
| Native graph, four threads | 8.014920 | 3.394007 | PASS |

Both candidates match all 297,074 ordered integer commits, checksum
`0xd687fdc5`, and 396,778 total / 396,768 work cycles. These are simulation-work
times with the tracing caveat above. The ELF SHA-256 is
`38ced8a38d6101aef1ab35e1e534a7bcb924dca41da5580a1e6fd2e3bc0c7a8a`.
Reports are under `<work-root>/qualification/<mode>/comparison.json`.

## Full comparison (2026-10-24)

The completed `.compare.sh --trials 1 --timeout 21600` run reports the following.
Each passing row has 396,778 total cycles. The Verilator row uses the first
successful comparison pair, as in the script's console table.

| Mode | Build seconds | Work seconds | Result |
| --- | ---: | ---: | --- |
| Original Verilator | 236.643 (cached) | 7.938897 | PASS |
| Default C++HDL runtime | 12623.938 | 2791.999715 | PASS |
| Only `--optimize-combs` | — | — | BLOCKED: initialization in `_assign()` |
| Native graph, one thread | 232.644 (cached) | 4.593998 | PASS |
| Native graph, four threads | 252.053 (cached) | 3.108216 | PASS |

All passing candidates match checksum `0xd687fdc5`, 396,768 work cycles,
10 reset cycles, and all 297,074 ordered integer commits. Compilation and
initialization are excluded from the work times. The tracing caveat still
applies. Native timings reproduce the earlier result; the current default
runtime is slower than the older 1089.619771-second reference. Its timing belongs
to the current bounded-memory build policy, which also affects emitted callback
code. The older default-runtime number must not be used for this binary.

The default build was cold and reproduced the exact binary from the interrupted
pre-reboot build. It took 3 hours 30 minutes, with peak sampled process-group
RSS of 5,021,487,104 bytes. Comb-only collection completed all 40 checkpoints,
including automatic splits down to individual aliases, before the merged
optimizer rejected `tandem_exit = logic<32>(0)`. That failed stage took
4045.533 seconds with peak sampled RSS of 4,090,195,968 bytes. Its original
diagnostics are preserved in `<work-root>/qualification/comb-full-collection.*`.

The comparison entry point now defaults to the single timed trial used here.
Its real argument parser was checked with both the default and an explicit
`--trials 3` override. Additional trials remain opt-in; five trials of this slow
default runtime require a larger timeout, as documented above.

All 14 entry points pass shell syntax and `--help` checks. The four available
standalone run scripts also pass with the default 2,000,000-cycle limit, stopping
at exactly 396,778 cycles with the same workload checksum and ELF hash. The
separate default-runtime wrapper check took 2743.072348 work seconds; it does not
replace the paired comparison timing above. `.build_all.sh` propagates the
comb-only compiler failure, and the comb-only run script correctly refuses to
run without its successful build stamp. All 60 helper tests and both CTest
checks pass. Disposable build intermediates are cleaned; runnable binaries,
source snapshots, reports, and small verified collection checkpoints remain
under the work root for reuse.

The full comparison JSON is `<work-root>/comparison.json`. Temporary artifacts
and reports disappear when `/tmp` is cleared; the scripts regenerate them.
A failed aggregate comparison exits nonzero even when the other four rows pass.

## Comb-only correctness follow-up (2026-10-24)

Four optimizer fixes address blockers exposed by the full model:

- Preserve ordinary `_assign()` storage initialization while extracting port
  bindings; initialization still executes in the original setup method.
- Use AST-resolved member references when a nested local shadows a field,
  rather than treating every occurrence of that name as a local variable.
- Invalidate procedural-comb memoization for dependencies mutated during
  `_work()`. Complete writes alone do not prove stability within that phase.
- Preserve dependent port initializers from primary template definitions.
  A concrete specialization can identify the port type without retaining its
  initializer AST. Losing the initializer incorrectly made the debug response
  FIFO output constant zero, preventing DMI initialization.

Focused regressions cover ordinary and L1 optimization, direct and serialized
collection, and both `-O0` and `-O2` compilation. The optimizer/workflow CTest
selection passed all 24 tests (including 61 workflow helper checks).
Fresh full-model collection and a development relink restored the first 100
cycles of DMI traffic exactly to the ordinary runtime's trace. This is only a
startup check, **not** full matmul equivalence or a performance qualification.
A host reboot cleared `/tmp` during the subsequent full comparison; no completed
comb-only timing or full commit-validation result was recovered from that run.
The historical table above must not be read as qualification of these fixes.

## Comb-only qualification (2026-10-25)

After rebuilding the cleared workspace with the preparation, conversion,
Verilator and comb-only build scripts, the full comparison passes:

| Mode | Simulation work (s) | Total cycles | Result |
| --- | ---: | ---: | --- |
| Original Verilator | 7.812545 | 396,778 | PASS |
| Only `--optimize-combs` | 2603.161349 | 396,778 | PASS |

This is one timed trial after separate full integer-commit validation, pinned
to CPU 2. Both execute the same 32×32 matmul, checksum `0xd687fdc5`, with
396,768 work cycles and 10 reset cycles. All 297,074 ordered integer commits
match with no difference. Validation with C++ commit logging took
2699.498013 seconds; that is not the timed-trial value. The original Verilator
tracing caveat still applies. Comb-only is **333.20× slower** in this comparison:
these changes restore correctness, not competitive simulation performance.
Default C++HDL and the native-graph modes were not rerun for this table.

The standalone Verilator and comb-only run scripts also pass with their normal
2,000,000-cycle limit, terminating naturally at 396,778 cycles. Their separate
work times are 7.964676 and 2601.887949 seconds respectively, not replacements
for the paired timings above. Temporary object backups and validated commit
traces were removed; binaries, build stamps, source snapshots and reports remain
under `/tmp` (about 395 MiB for this two-mode workspace).

The actual comb-only build script completed all 68 compilation units and
linked without a handwritten model replacement. The RAM guard stopped one
cold-build attempt after 11481.669 seconds; resuming reused verified collection
and object checkpoints and completed in 7355.042 seconds, with peak sampled
process-group RSS of 3,852,054,528 bytes. This resumed duration is not a cold
build cost. Verilator built in 227.422 seconds. The rebuilt comb-only executable
has the same SHA-256 as the pre-reboot development relink:
`ce392e171530228c94b55c57e44873650ba8220fc6fd545045b977b32df97237`.
The shared ELF hash remains
`38ced8a38d6101aef1ab35e1e534a7bcb924dca41da5580a1e6fd2e3bc0c7a8a`.

All 24 optimizer/workflow CTest checks passed again after reboot. The full
pair report is `<work-root>/comparisons/cpphdl-optimize-combs/comparison.json`.
To reproduce just this pair after running its four build/preparation scripts:

```sh
root=${CVA6_WORK_ROOT:-/tmp/cpphdl-cva6-$(id -u)}
python3 -B compare_matmul.py \
  --verilator "$root/verilator/Variane_testharness" \
  --cpphdl "$root/cpphdl-optimize-combs/run" \
  --elf "${ELF:-$root/elf/matrix_multiply.riscv}" \
  --output "$root/comparisons/cpphdl-optimize-combs" \
  --check-commits --trials 1 --max-cycles 2000000 --timeout 7200 --cpu 2
```

Choose an allowed CPU instead of 2 if necessary. The qualification run wrapped
this command in the workflow's RAM/disk/timeout guard. `.compare.sh` remains the
guarded entry point for building and comparing all five modes.

## Native-packed default qualification (2026-10-25)

Fresh ordinary HDLCPP conversion completed with zero failures. Native packed
aggregate storage is now the common C++20+ runtime default, without per-mode
layout macros or special header inclusion order. After rebuilding, this full
three-backend comparison passed:

```sh
./.compare.sh --work-root /tmp/cpphdl-cva6-1000 \
  --modes verilator cpphdl-optimize-combs cpphdl-native-graph \
  --trials 1 --timeout 21600
```

| Mode | Simulation work (s) | Total cycles | Result |
| --- | ---: | ---: | --- |
| Original Verilator | 8.029921 | 396,778 | PASS |
| Only `--optimize-combs` | 1832.095888 | 396,778 | PASS |
| Native graph, one thread | 4.608362 | 396,778 | PASS |

These are one-trial work times, excluding build and setup, pinned to CPU 2.
The table follows `.compare.sh`: its Verilator row comes from the comb pair;
the separate native-graph pair measured Verilator at 7.559071 seconds.
Both candidates match all 297,074 ordered integer commits and the same
32x32 matmul checksum `0xd687fdc5`, with 396,768 work cycles plus 10 reset
cycles. Original Verilator retains trace formatting; candidate commit logging
is enabled only during separate validation. This is not a trace-free simulator
kernel comparison.

Comb work time is 29.6% below the historical 2603.161349-second result, but
still 228.16 times its paired Verilator time. That historical reduction covers
the helper-dependency, continuous-loop fusion, and native-storage changes
together; it does not isolate the storage change. The small arbiter improvement
must not be extrapolated to the entire CPU.

The first comb build stopped at the RAM safety guard. A compiler-only allocator
retry compiled the failed root binding unit within the guard, and the resumed
build reused verified object/collection checkpoints. It completed in
10504.504 seconds with peak sampled process-group RSS of 4,204,892,160 bytes;
this is not a cold-build time. The original Verilator and native-graph builds
took 249.184 and 238.278 seconds, respectively, and were verified cached builds
on the final comparison retry. No handwritten hardware replacement was used.
Temporary build objects and validated traces were removed automatically.

Reports: `<work-root>/comparison.json` and
`<work-root>/comparisons/{cpphdl-optimize-combs,cpphdl-native-graph}/comparison.json`.
The shared ELF SHA-256 is unchanged from the preceding qualification.
