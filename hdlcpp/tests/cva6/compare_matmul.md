# Equivalent-work CVA6 matmul comparison

Build both testbenches with `CVA6_BENCH_STATS` support, then run:

```sh
python3 hdlcpp/tests/cva6/compare_matmul.py \
  --verilator /path/to/cva6/work-ver/Variane_testharness \
  --cpphdl /path/to/fresh/model/run_cpphdl_testharness_opt \
  --output /tmp/matmul-comparison --cpu 2
```

The default ELF is the adjacent `matrix_multiply.riscv`. It checks integer
results 16, 24, 28, and 42, then prints `PASSED`. Both simulators must run the
same ELF, print that line, exit successfully through `tohost=0`, and report
consistent, identical reset/work/total clock counts. The script first runs
Verilator to determine the completion count, then gives cpphdl exactly that
cycle budget. A timeout is a validation failure, not an equivalent-work sample.
If validation fails, the script exits nonzero and does **not** report a speed
ratio. It does not replace or emulate the hardware to obtain a passing result.

The native command also receives `+elf_file=PATH` after the binary argument.
This initializes the RTL tracer's `tohost` address from the same ELF, matching
cpphdl's tracer configuration. Without it, the native driver can keep running
until a later DTM poll even though the guest has already completed.

Once validation passes, three alternating trials per simulator measure
`steady_clock` time inside the simulation loop. Initialization, ten reset
clocks, ELF loading, conversion, and compilation are excluded. Process wall
time is recorded separately. `--trials`, `--max-cycles`, `--timeout`, and `--elf`
are configurable; omit `--cpu` if CPU affinity is unavailable. Avoid other
builds or simulations during timing.

`comparison.json` records executable/ELF hashes, commands, validation status,
cycle counts, and individual timings. Each run has a separate working directory
and `run.log`. Trace/override environment variables are cleared so inherited
debug probes or fixed-cycle overrides cannot silently change the measurement.
The script compares this program's self-checked output and completion clocks;
it is not a proof of equivalence for every architectural state or every input.

The original Verilator testbench also writes RTL diagnostic trace files.
hdlcpp currently removes diagnostic `$fwrite`/`$display` calls, so agreement of
the guest self-check and cycle counts does not imply identical host trace I/O.
Report this difference alongside timings; do not describe a trace-enabled RTL
run and a trace-free C++ run as having identical host-side diagnostic work.

Fresh full conversion uses the maintained conversion flow, including its
existing CVA6 compatibility maps:

```sh
CVA6_SRC=/path/to/cva6 CPPHDL_OUT=/absolute/path/to/fresh/model \
  CPPHDL_CVA6_NATIVE_HARNESS=1 CPPHDL_COMB_OPTIMIZER_MODE=l1 \
  bash hdlcpp/tests/cva6/convert.sh
CVA6_SRC=/path/to/cva6 CPPHDL_OUT=/absolute/path/to/fresh/model \
  CPPHDL_CVA6_NATIVE_HARNESS=1 RISCV=/path/to/riscv JOBS=1 \
  bash hdlcpp/tests/cva6/build.sh
```

Use a new output directory: full conversion recreates it. Do not force reuse
of serialized comb collections after changing the compiler. Rebuild the
Verilator testbench driver after changing its measurement code. The generated
RTL model does not need a hand-written replacement.

Comparison-gate regression tests:

```sh
python3 -B -m unittest discover -s hdlcpp/tests/cva6 -p test_compare_matmul.py -v
```

## Updated ordinary C++ / native graph experiment

Use a read-only snapshot of the same CVA6 RTL for both compilers, fresh tool
binaries, and an owned `/tmp` directory. Keep all generated headers, executables,
traces and build logs there, build serially, and remove that directory after
recording the report. Do not regenerate into either checkout.

```sh
work=$(mktemp -d /tmp/cva6-native.XXXXXX)
# Prepare $work/source from the CVA6 source snapshot used for Verilator.
CVA6_SRC="$work/source" CPPHDL_OUT="$work/ordinary" \
  CPPHDL_CVA6_NATIVE_HARNESS=1 CPPHDL_CVA6_SKIP_OPTIMIZE=1 \
  HDLCPP=/path/to/fresh/hdlcpp CPPHDL=/path/to/fresh/cpphdl \
  TARGET=cv32a6_imac_sv32 RISCV=/path/to/riscv HDLCPP_JOBS=1 \
  bash hdlcpp/tests/cva6/convert.sh
python3 -B hdlcpp/tests/cva6/build_native_graph.py \
  --ordinary "$work/ordinary" --output "$work/native" \
  --cpphdl /path/to/fresh/cpphdl --cxx /path/to/clang++ --riscv /path/to/riscv
python3 -B hdlcpp/tests/cva6/compare_matmul.py --native-graph \
  --verilator "$work/verilator/Variane_testharness" \
  --cpphdl "$work/native/run" --output "$work/comparison" --cpu 2
```

There is no graph flag on hdlcpp and no legacy L1 optimization/concretization
pass. The native builder lowers the actual converted headers, uses one native
host lane, bounds function size to 512 nodes, and verifies the original SRAM's
64-bit rows and 33,554,432-row depth before preloading it. The wrapper exposes
only testbench reset, RTC, termination configuration and RVFI observations;
it does not replace hardware. Graph commits occur at the driver's strobe, not
at its preparation call. `--native-graph` on the comparator requires actual
graph commits to equal the reported total edge count, rejecting extra hidden
transactions as well as ordinary output/cycle mismatches.

For an additional architectural check, run the candidate once with
`CPPHDL_TRACE_COMMITS=1 CVA6_BENCH_STATS=1`, redirect its combined output to a
log, then compare it with the reference run's `trace_rvfi_hart_00.dasm`:

```sh
python3 -B hdlcpp/tests/cva6/check_commit_trace.py \
  --verilator "$work/comparison/validation-verilator/trace_rvfi_hart_00.dasm" \
  --cpphdl "$work/validation-commits.log" --output "$work/commits.json"
```

The checker compares instruction order, PCs, instruction bits and integer
destination/value records, ignores nonarchitectural data associated with `x0`,
and rejects missing, extra, duplicated, malformed or trapping native commits.
This supplements the successful matmul/output/cycle gate; it does not compare
all internal state, memory write records or floating-point registers. Keep
commit tracing out of timed runs.

The smaller production-bus experiment supports `check_context_replay.py
--native-graph --model "$work/ordinary"` (without `--context`). It regenerates
ordinary headers before C++ graph lowering; the old L1 comparison remains
available without that flag. Use the **completed**, metadata-converged model:
copying its traits before regeneration finishes can leave projected input
bindings unconnected. `check_native_graph.py` remains the explicitly separate
direct-SV graph reference, not the ordinary C++ chain or a full-core result.

The updated frontend retains module-array interface identities, treats packed
array proxy assignments as logical storage writes, and recognizes `std::as_const`
as a storage-preserving alias. Single-lane chunked emission preserves ordered
random/JTAG/debug callbacks; settling must not execute them. Dedicated regressions
check callback values, projections, ordering and invocation counts.

Full-design profiling also exposed repeated global SSA-map copies at dynamic
branches. `CppGraphEnvironment.h` replaces those copies with immutable AVL paths:
checkpoints share a root, updates copy only the search path, and merges visit only
changed subtrees. This preserves old versions for getter invalidation and branch
fallbacks rather than weakening either check. The standalone differential test
compares insertion, deletion, snapshots, restoration and iteration with `std::map`.
Its snapshot microbenchmark is **frontend bookkeeping**, not simulation runtime.

Wide-value regressions additionally cover bit 64 and 65-bit carry, including
assignment-context widening. hdlcpp must not truncate a wide operand to `uint64_t`
before selecting or adding it. Test the generated ordinary C++, native graph and
original SV against the same 4,096-case oracle before regenerating affected sources.

The symbolic-arithmetic regression also reproduces the RV32 multiplier's
`$signed({operand[Width-1] & sign, operand})` operands. The concatenation is
33 bits at `Width=32`, not two copies of the operand width; its signed value must
then be extended to the 64-bit assignment context before multiplication. Fixing
only the signed-cast width still truncates the product to 33 bits. The complete
fix passes 1,024 random/extreme cases for each of the 8-, 64- and 65-bit fixture
configurations in ordinary C++, native graph and original-SV Verilator, with
ASan/UBSan enabled for the C++ representations.

After a generator change, trait-resume also regenerates missing headers and
headers older than the generator or their RTL source. Do not mix selectively
regenerated arithmetic with stale full-core headers. The native builder records
compiler/header input hashes and rejects inputs changed during compilation.

The full-core retry additionally exposed a packed boot-ROM initializer and
driverless RVFI PMP diagnostic aggregates. `ConstPackedRom` covers immutable
packed-array initialization, all valid indices and the invalid-index zero
fallback in ordinary C++, original-SV Verilator and both native emission modes
(4,096 cases each). Mutable, input-dependent and recursive initializers remain
rejected. The driverless-variable regression covers packed structs/arrays,
field-wise driven siblings, interface pins, unused unpacked scratch storage and
initial-block setup variables (six 4,096-case runs). In particular, do not emit a
268 MiB assignment to the SRAM model's unused `init_val` scratch array, or freeze
the tracer's `SIM_FINISH` / `TOHOST_ADDR` setup as structural constants.

Clocked blocking variables also need explicit retention in the ordinary C++
lifecycle. For example, disabling SimJTAG removes every callback write to its
output pins, but those pins still retain their two-state initial value; they are
not undriven combinational wires. hdlcpp emits a self-hold at `_work` entry for
clocked blocking storage, alongside the existing nonblocking next-state holds.
This preserves immediate reads after blocking assignments without inventing
reset assignments or calling disabled DPI callbacks. `BlockingHold` compares
disabled and active capture, repeated resets, hold cycles and immediate readback
against ordinary C++, original SV and both native emission modes.

The DMI TAP next-state getter exposed another false undriven dependency: all 16
values of its four-bit state are covered, but generated C++ promotes the selector
to `uint64_t` and includes an empty default. CppHDL now proves case coverage using
resolved constant selector bits (at most 12 independent variable bit positions).
An exhaustive switch uses a real case environment as its merge fallback, not
the uninitialized pre-switch scratch value. The isolated generated DMI TAP fails
before this change and lowers afterward. `ExhaustiveSwitch` additionally checks
all 4,096 input combinations, noncontiguous masks and known-one selector bits in
ordinary C++, original SV and both native modes; missing labels or missing writes
remain rejected rather than being silently initialized.

The optional instruction-trace encoder's `address` signal is genuinely latched
by its SV `always_comb` block: invalid lanes have no assignment. Do not replace
that latch with a zero default. For a fully bound native executable, CppHDL now
computes observable sequential liveness before validating combinational drivers;
diagnostic state with no output, callback or memory-effect consumer can disappear.
This is requested internally by `--final-native`; partition and synthesis
lowering retain their original state. Clock contracts are checked first, and
observable incomplete writes still fail. The separate RVFI finisher and exported
commit records remain roots, as do all callbacks, memory writes and bounds checks.
SV trace-file I/O is still absent from the ordinary C++ model; report that host-I/O
difference separately from the architectural matmul/cycle comparison.

Function chunking alone does not bound host compilation memory: LLVM's
interprocedural optimizer still sees every function in the 62 MiB full-core
header. `--split-chunks=64` moves generated chunk definitions into separate
`model.part.*.cpp` translation units, with explicit true/false instantiations for
transactional callbacks. The native tool links those files automatically;
manual consumers must link them too. CVA6's builder enables this alongside
`--chunk-size=512`. This changes compilation boundaries, not simulated work.
Small regressions cover state, SRAM, callback order/counts and effect-free output
settling. A failed full build can use `--resume-graph` only after frontend input
hashes are checked and the stored graph reproduces the recorded unsplit model
byte-for-byte; no manually edited generated model is accepted.

The first executable full native model timed out without passing matmul; that
run is not an equivalent-work performance comparison. A short instruction/state
trace narrowed the stall to dynamic accesses into packed scoreboard records.
For example, selecting the 271-bit `sbe` member of a 274-bit record array requires
offsets `0, 274, 548, ...`, not multiples of 271. The old frontend enumerated
offsets in steps of the selected field width, silently returning zero for most
records; partial dynamic writes had the same error.

CppHDL now carries element-stride/residue information through indexing, member
selection and fixed slices. Nested dynamic dimensions combine their strides
conservatively; reads and writes enumerate reachable locations, independently
of field width. This also avoids scanning every bit of a large record bank when
selecting a one-bit member. `DynamicStructIndex` first reproduces the failure
against the frozen old compiler, while ordinary C++ and original-SV Verilator
pass. The corrected compiler passes 4,096 cycles in each of ordinary C++,
Verilator, native header-only and separately compiled native modes, including
two-dimensional packed/unpacked banks and independent partial field writes.

The repaired dynamic indexing model boots, but its first full matmul attempt
fails after 3,521 cycles. The first 507 architectural commits agree with the
original SV; the next load at `0x80003086` reads zero instead of two from
`0x80025004`. The main SRAM still contains the correct word. A refill into a
different cache way overwrites the first way's data while its directory tag
remains valid. This is a separate hdlcpp conversion error, not evidence of an
equivalent-work speedup.

`hpdcache_way_to_data_ram_word` returns a narrow cast of `i % dataWaysPerRamWord`.
The old typed-expression emitter first narrows both arithmetic operands to the
return width: with four ways, `logic<2>(4)` is zero. SV evaluates the modulo at
the operand/context maximum width and only then narrows the result. The
`NarrowArithmeticCast` regression tests dependent typedefs, packed configuration
fields, division, modulo, nested casts, intermediate wrapping and widened
multiplication. Original-SV Verilator passes 4,096 samples at width two before
the fix, while the old ordinary C++ reports sanitizer division-by-zero failures
and a wrong result at sample zero.

The corrected emitter uses the operand/context maximum for division/remainder
and applies the destination width only to the completed result. Addition,
subtraction, multiplication and bitwise operations can keep their destination
width because those operations commute with final truncation. Dependent maxima
use `std::max<uint64_t>` rather than duplicating their operands in a ternary:
the initial ternary experiment exhausted memory while converting the CSR
module and was replaced. The small regression additionally nests 24 additions
under a narrowing division, checks that the generated header stays below
1 MiB, and validates the output with address/undefined-behavior sanitizers.
The bounded emitter passes all twelve combinations of widths 2/3/5 and
ordinary C++, original-SV Verilator, native header-only and split-native modes
(4,096 samples each), plus the deep-expression test. Existing symbolic
arithmetic tests pass at widths 8/64/65 in all three backends, including
context-widened multiplication and signed multiplication.

The first full retry of the broad maximum-width emitter also exposed a separate
width-query bug in the CSR mask expression. The textual cast detector treated
`HS_DELEG_INTERRUPTS[XLEN-1:0] | CVA6Cfg.XLEN'(value)` as one sized cast, turning
the preceding value/slice into a non-constant C++ template width. Cast-target
extraction now requires an actual cast syntax node; calls and binary/unary
expressions cannot supply a cast target from embedded casts. The upgraded small
fixture uses dotted packed-configuration cast widths to reproduce the invalid
generated C++, then checks its mask against original SV.

The same fixture checks a narrowing division inside a function with two
32-bit arguments. hdlcpp previously emitted an inherited second argument as
`bool`. Direction-less, type-less arguments now inherit the preceding type,
and expression width queries use known local/formal types instead of falling
back to one bit. The full model must be regenerated before comparing it; no
generated cache, CSR or hardware implementation is patched by hand.

Regeneration also exposed malformed instance-array dimensions in
`cva6_shared_tlb`: extracting the first `<` from emitted comparison text picked
the shift inside a 32-bit operand mask. Generate limits now come from the
relational syntax node's right operand, with an inclusive `<=` bound increased
by one. Constructor/method loop headers carry those resolved limits into
instance-array emission instead of reparsing formatted C++. The final small
fixture includes nested generated instances, a shifted upper bound and an
inclusive inner bound, and passes all twelve backend/width combinations plus
the deep-expression test. The existing converged port/type metadata is reused
only for this emitter-only correction, while all 284 sources are re-emitted.

## Completed full-core checkpoint: October 11, 2026

The fully regenerated ordinary-C++/native-graph chain now completes the original
integer matmul. All 284 conversion sources were emitted without conversion
failures; the full model was built from unchanged converted headers. The SRAM
remains 64 bits by 33,554,432 rows. There are no handwritten hardware replacements.

Both simulators print `PASSED`, terminate through `tohost=0`, and simulate
**46,264 total cycles: 10 reset plus 46,254 work cycles**. The candidate's actual
graph commit count also equals 46,264. The independent diagnostic run matches
all **21,285 integer architectural commit records**, not merely a prefix.

Three alternating trials, pinned to CPU 2, give these simulation-loop times:

| Trial | Original-SV Verilator | Ordinary C++ / native graph |
| --- | ---: | ---: |
| 1 | 0.794102515 s | 26.909734604 s |
| 2 | 0.806077422 s | 27.020316519 s |
| 3 | 0.820039363 s | 27.078164112 s |
| Median | **0.806077422 s** | **27.020316519 s** |

The correct full native model is **33.52 times slower**, not competitive yet.
These are fresh matched-work measurements, not a ratio against an older timing
or a failed/timed-out native model. Build, initialization, ELF loading and reset
time are excluded. Verilator still writes its original RTL diagnostic trace
files, while the converted model omits those host-I/O calls.

Provenance for the completed measurement:

| Input | SHA-256 |
| --- | --- |
| Matmul ELF | `a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c` |
| Verilator executable | `6ce48f07006a257b8de9e3032254f2298761c22d525c548d8c26d2b7c77ef5c0` |
| Native executable | `f405662589bf1931808995a4f7340a0b0fa930c2df534f9ea19499ab479fa38e` |

The freshly regenerated smaller AXI production-bus replay also passes all
46,264 checked boundary cycles and the same `5d674e8806b5fa98` checksum. Over
462,640 repeated replay cycles, three-trial medians are 1.601615311 s for
Verilator and 1.564686474 s for native graph, only a 2.31% reduction. This is a
separate bus-only benchmark and plainly does **not** represent the full-core gap.
No optimization is retained or credited on the basis of that sub-10% difference.

The full graph contains 200,604 scheduled nodes, 941 state groups and 380
generated function chunks. Source inspection confirms three complete scheduled
evaluations per normal guest cycle: preparation calls `eval(false)` and `step()`
calls both `evaluate<true>()` and `evaluate<false>()`. Hardware perf counters are
unavailable (`perf_event_paranoid=4`). A separate, untimed GDB sampling run
captures 20 stacks, all inside generated graph chunks: 13 settling evaluations
and seven commit evaluations. That instrumented run still passes in 46,264
cycles. These coarse samples locate the execution cost in generated graph
evaluation, not conversion/template compilation, and suggest testing reduced
or dependency-directed settling next. They do not prove that removing a pass
is safe, nor that redundant settling alone explains the entire 33.52x gap.

The final small arithmetic/generate fixture passes 13 configurations with
4,096 samples each. The widened/signed arithmetic fixture passes nine
configurations with 1,024 samples each. All 34 CVA6 harness/checker unit tests,
including the compiled native adapter test, pass without skips.

## Timing and representation audit: October 12, 2026

The suspected 0.8 s Verilator timer was checked independently, without
regenerating its hardware model. A diagnostic testbench and top-model entry
point were rebuilt using the exact original compiler/link commands and original
generated hardware object archive. Only host counters were added. Counters are
inside `Variane_testharness::eval_step()`, not inferred from the loop's reported
cycle count. The original executable remains unchanged.

Three audited full runs report 92,508 actual model-entry calls: 46,254 low-clock
calls, 46,254 high-clock calls and 46,254 rising edges, excluding ten reset
cycles. All runs pass the same ELF at 46,264 total cycles, and all 21,285 integer
commit records match the independently captured native run. The internal
steady-clock times are 0.761627130, 0.759843089 and 0.799854861 s; independent
`std::clock()` CPU times are 0.761570, 0.759754 and 0.799715 s. External process
wall times are 1.091, 1.085 and 1.123 s, including initialization/loading/reset.
Unmodified Verilator runs in the same audit take 0.785086237, 0.828134483 and
0.772190656 s inside the simulation loop. Thus there is no evidence of a timer
scaling error, skipped clock loop, or early termination masquerading as full
completion. This workload is only 21,285 committed integer instructions, not
millions of matmul instructions.

As another control, capped runs execute 990, 9,990 and 24,990 work cycles in
0.017443399, 0.150037524 and 0.407562341 s, respectively. Their 181, 6,107 and
17,872 integer commit records exactly match the corresponding native trace
prefixes. These deliberately incomplete runs are timing/counter diagnostics,
not equivalent-work performance comparisons.

### What native graph actually evaluates

Chunking is a compilation boundary, not partial-design simulation. Sampled
per-chunk timers show every one of the 380 original chunks being called in
every sampled pass: 361 commit calls per chunk and 722 combined settling calls
per chunk. The full schedule is executed once for commit and twice for
settling per normal guest cycle. All original function bodies were verified
byte-for-byte after removing the inserted timing scopes. The instrumented full
run also passes at 46,264 cycles and matches all 21,285 integer commit records.
Its elapsed time is not used as an uninstrumented performance result.

The strongest representation problem is the HPDcache data SRAM. Four banks
account for **149,624 of 200,661 graph nodes** (74.57%), including 110,608 muxes
and 34,824 equality tests. Chunks whose dominant named-node scope belongs to
those banks account for **70.41% of sampled chunk elapsed time**. This is a
coarse scope attribution, not an exact instruction-level profile of all host
execution. In contrast, the original Verilator generated code performs
conditional byte updates to `mem[selected_row]`; it does not expand each
dynamic write into updates/muxes for every possible cache row.

hdlcpp already emits `cpphdl::memory` for a directly declared, sequentially
indexed unpacked array. However, `varType()` does not recover the unpacked
dimension when it is hidden in a typedef. CVA6's
`hpdcache_sram_wbyteenable_1rw` declares `mem_t` as an unpacked array of packed
words and then declares `mem_t mem`. The generated C++ becomes `reg<mem_t>`,
and C++ graph extraction expands dynamic reads and byte writes over all rows.
This is a missed RAM-lowering optimization, not a currently demonstrated
functional mismatch.

### Controlled RAM reproducer

These declarations have identical SV behavior but take different conversion
paths with the current tools:

```systemverilog
typedef logic [127:0] mem_t [Depth];
mem_t mem;                         // currently becomes reg<mem_t>
```

```systemverilog
logic [127:0] mem [Depth];          // becomes memory<logic<128>,1,Depth>
```

`RamAudit.sv` exercises the same synchronous, byte-enabled read/write behavior
as the CVA6 data RAM. Each variant first clears all rows and checks 4,096 random
cycles against an independent oracle, including disabled accesses, retained
read output and individual byte masks. Then it measures 100,000 additional
cycles, with three trials pinned to CPU 2. All variants at each depth have the
same checked-cycle count and measured-output checksum. Both native variants
use the same ordinary hdlcpp-to-C++/native-graph toolchain, three evaluations
per cycle, chunk size 512 and split size 32; only the equivalent SV declaration
form changes. No full-core hardware was replaced.

| Rows | Original-SV Verilator | Native, typedef array | Native, preserved RAM |
| --- | ---: | ---: | ---: |
| 16 | 0.008111210 s | 0.077193906 s | 0.014195126 s |
| 512 | 0.008172688 s | 3.418842651 s | 0.014322870 s |

At 512 rows, preserving RAM gives a **238.7x** improvement in this isolated
RAM benchmark and is **1.75x** the Verilator time. The typedef version grows
from 1,200 to 37,408 graph nodes as depth grows; the RAM version stays at
65 source / 48 scheduled nodes for both depths. Checksums are
`2b79640258d66808` at 16 rows and `5ca3049d02448eb1` at 512 rows.
This establishes a representation problem, not just a function-call/chunk-size
problem. It does not establish a 238.7x full-core speedup.

The first production change to test is generic typedef-aware RAM inference in
hdlcpp, preserving unpacked dimensions and nonblocking byte-write semantics
through aliases, rather than rewriting CVA6 RTL or replacing its RAM by hand.
The existing native-graph memory representation already provides the fast
path. Separating output-only settling from full next-state scheduling is a
second issue: the current chunk interface still invokes every chunk in both
settling passes. Any such change needs the same full-cycle/commit validation;
simply deleting a pass without checking changed inputs/observations is unsafe.

The complete audit, diagnostic sources, commands, profiles and RAM trial
records are retained under `/tmp/cva6-native-update.I0aRD3/audit-20261012`.

## Larger integer matmul workload

`matrix_multiply_large.cpp` provides a configurable square integer matmul:
32x32 by default, with 32,768 multiply-accumulates and a 12 KiB total matrix
working set. It checks every result against an independent closed-form sum
after each repetition, hashes all output elements and prints a single
`CVA6_MMUL size=... repeats=... macs=... checksum=...` signature followed by
`PASSED`. Repetitions change the input offset, and the multiplication kernel
is a separate non-inlined function. The original tiny workload/ELF is unchanged.

Build and compare without regenerating either CPU simulator:

```sh
work=$(mktemp -d /tmp/cva6-matmul-large.XXXXXX)
python3 -B hdlcpp/tests/cva6/build_matrix_benchmark.py \
  --cva6-source /home/me/cva6 --riscv /home/me/riscv \
  --output "$work/elf" --size 32 --repeats 1
python3 -B hdlcpp/tests/cva6/compare_matmul.py --native-graph --check-commits \
  --verilator /path/to/Variane_testharness --cpphdl /path/to/native/run \
  --elf "$work/elf/matrix_multiply.riscv" --output "$work/comparison" \
  --max-cycles 2000000 --trials 3 --cpu 2 --timeout 1800
```

Sizes 1..128 and repetitions 1..1024 are supported; the largest combinations
can take a very long time on the currently unoptimized native model. The RV32
guest is compiled at `-O2`, with `rv32imac_zicsr_zifencei`, freestanding startup
and the existing linker/runtime. All objects, ELF, disassembly and build/input
hashes go into the explicit `/tmp` output directory. The builder does not invoke
the simulator build or write into either checkout. Keep CPU simulator binaries
identical throughout a comparison.

The comparator checks dimensions, repetitions, MAC count and checksum as well
as successful termination and identical reset/work/total cycle counts.
`--check-commits` additionally compares PCs, instruction bits and integer
register-write records in the validation runs; missing or different traces
prevent timed trials and speed-ratio reporting. Native commit tracing is enabled
only for validation and explicitly absent from timed trials.

Host regression tests exercise sizes 1, 3, 16, 32 and 128 and multiple
repetitions; small cases also compare the expected formula with an independent
triple-loop product. The RV32 disassembly retains the multiplication loop.
For the 32x32 ELF, the original-SV RVFI trace independently records **32,768
executions of the kernel's `mul` at PC `0x8000303c`**, confirming that the guest
performs the advertised computation rather than printing a folded answer.

### Completed 32x32 CPU comparison: October 12, 2026

Both existing single-core simulators execute the identical RV32 ELF, print
`CVA6_MMUL size=32 repeats=1 macs=32768 checksum=0xd687fdc5` and `PASSED`,
and exit successfully through `tohost=0`. They simulate exactly **396,778
total cycles: 10 reset plus 396,768 work cycles**. Native actual graph commits
equal that total. All **297,074 integer architectural commit records** match,
including the 32,768 kernel multiply instructions.

| Trial | Original-SV Verilator | Ordinary C++ / native graph |
| --- | ---: | ---: |
| 1 | 8.095937854 s | 205.824608618 s |
| 2 | 8.184859340 s | 206.745263988 s |
| 3 | 7.839129118 s | 207.554663785 s |
| Median | **8.095937854 s** | **206.745263988 s** |

Native graph is **25.54x slower** on this larger workload. These are alternating,
CPU-2-pinned simulation-loop timings, excluding conversion, compilation, ELF
loading and reset. They include the complete guest startup, matrix
initialization, multiplication, checking and completion protocol, not just the
multiply kernel. Native instruction tracing is absent from timed trials;
Verilator retains its original RTL diagnostic file output. The traced native
validation takes 225.912061143 s and is not used in the timing median.

No CPU simulator or hardware model was regenerated or changed for this test;
only the guest workload and comparison validation were extended. The original
tiny ELF is preserved. Differences from its previous timing ratio are not
credited as a compiler/native-graph optimization, particularly because this
guest is built at `-O2` whereas the older tiny build used default guest
optimization settings.

The new ELF SHA-256 is
`38ced8a38d6101aef1ab35e1e534a7bcb924dca41da5580a1e6fd2e3bc0c7a8a`.
Simulator hashes remain the same as the October 11 full-core checkpoint.
All 37 harness/workload unit tests pass without skips. Build metadata,
instruction audit and the complete timing/validation report are retained in
`/tmp/cva6-matmul-large.1R7PeN`; temporary objects and bulky traces are cleaned
after saving a verified reference archive.

### Preserve inferred SRAMs through typedefs (October 12)

The production converter now resolves the outer unpacked dimension of a
sequentially indexed typedef before choosing storage. For example, the original
HPDcache declaration `typedef logic [NDATA-1:0][DATA_SIZE-1:0] mem_t [DEPTH];`
followed by `mem_t mem;` produces `memory<logic<DATA_SIZE>,NDATA,DEPTH>` rather
than `reg<mem_t>`. Packed-only, struct-valued, asynchronously reset, and
whole-array-accessed banks remain registers. This is generic storage
inference, not a cache-specific replacement or a direct SV-to-graph bypass.

The existing RAM transaction lowering preserves committed reads, nonblocking
byte-write merging, and held read output. Two support fixes are necessary:
ordinary `memory_row` accepts packed-array whole-row assignments element by
element, and the C++ graph frontend accepts scalar logic rows whose C++ object
has trailing padding (for example `logic<13>`). Multi-element rows still require
an exact physical stride; their padding must not silently become HDL state.

`tests/hdlcpp/TypedefRamChecks.py` checks local/chained/package aliases, 16- and
512-row depths, 128- and 256-bit rows, byte/full-row writes, ordinary C++ under
ASan/UBSan, header-only and split native code, and original Verilator. An
additional 13-bit directory-RAM case checks scalar padding. Each backend is
checked against an independent 4096-cycle RAM oracle. Depth must not increase
the native scheduled node count.

The unchanged original `RamAudit.sv` typedef case, rerun for 100,000 cycles and
three CPU-2-pinned trials, gives:

| Backend, 512 rows | Median simulation time |
| --- | ---: |
| Previous expanded typedef bank | 3.532678029 s |
| Automatically preserved RAM, split native | 0.014795232 s |
| Original Verilator | 0.008145307 s |

Checksums match (`5ca3049d02448eb1`). The automatic conversion produces 65 source
and 48 scheduled graph nodes instead of 37,408 source / 37,406 scheduled nodes:
238.77x faster than expanded storage, still 1.82x the Verilator time in split
mode. These are isolated RAM timings, **not a full-CVA6 speedup claim**.
Reports and frozen tools are under `/tmp/cva6-preserve-blocks.NVmfFf`.

Word-level add/multiply/divide graph primitives are retained. There is no
precision truncation, handwritten arithmetic replacement, or LUT expansion in
this change; wider arithmetic continues to use exact native-word limbs.

#### Full-core result with automatically inferred SRAMs

Every conversion source was regenerated from the frozen original SV snapshot
(284 sources, 287 generated definitions, zero conversion failures). Register
banks that escape as whole values, have asynchronous reset, or contain packed
struct entries are deliberately not inferred as RAM. Blocking-only writes also
retain their immediate-visibility representation. No generated cache header or
CPU implementation was patched by hand.

The complete ordinary-C++ to native-graph build succeeds. The graph drops from
200,661 source / 200,604 scheduled nodes to **41,816 / 41,690**. The memory table
contains four HPDcache data RAMs of 128 bits x 512 rows, eight directory RAMs of
26 bits x 256 rows, and the 80-bit MSHR RAM. Both original 256 MiB testharness
SRAM allocations remain unchanged. Native arithmetic still includes 86 native
word multiplies and seven divides; it is not a gate-level expansion.

The identical 32x32 ELF, one core, CPU 2, and three alternating timing trials:

| Backend | Median work time | Total cycles |
| --- | ---: | ---: |
| Original Verilator, fresh rerun | **8.094142412 s** | 396,778 |
| cpphdl native graph, inferred RAM | **19.745789464 s** | 396,778 |
| Previous expanded-bank native graph, reference run | 206.745263988 s | 396,778 |

The new native timing samples are 19.745789464, 20.654915462 and 19.057329527 s;
Verilator samples are 7.878553432, 8.094142412 and 8.176716645 s. Traced validation
is separate and is excluded from these medians. All **297,074 integer RVFI
commits match**, including order, PC, instruction, destination and writeback.
The 32,768-MAC workload signature and checksum `0xd687fdc5` match in every run;
both execute 10 reset and 396,768 work cycles.

Against the retained expanded-bank reference this is **10.47x faster / 90.45%
less simulation time**. The fresh Verilator median differs from the reference
by less than 0.03%, but the previous native row is a reference measurement, not
a newly rerun old binary. The candidate remains **2.44x slower than Verilator**:
RAM preservation removes the dominant expansion. This experiment does not
isolate the remaining evaluation overhead or attribute it to any unmeasured
arithmetic kernel.

The final isolated RAM rerun gives 3.499772965 s expanded, 0.014424586 s inferred,
and 0.007980864 s Verilator: **242.63x** over expanded storage, **1.81x** Verilator
in split mode, matching checksum and the same 65 / 48 native graph nodes.

Build time for the full native model is 270.905 s, not included in simulation
timings. The guarded regeneration/build/comparison pipeline peaks at 3.258 GB
RSS (decimal). The native executable SHA256 is
`bcb55e18b2ddefdeaa3fe447d7303ba58c28db0672da0cca0eadc9fa88f5ab0d`;
the Verilator and ELF hashes are unchanged from the retained larger-workload
reference. `comparison/comparison.json`, `native-ram/manifest.json`, and
`native-ram/graph/memories.tsv` record equivalence and provenance under
`/tmp/cva6-preserve-blocks.NVmfFf`.

### Next optimization target: phase-specific native graph liveness

Profiling the RAM-preserving model identifies redundant settling, rather than
RAM expansion or large arithmetic, as the next compiler-level target. These
are diagnostic experiments only: production generator and adapter code are
unchanged by this profiling investigation.

Linux `perf` counters are unavailable (`perf_event_paranoid=4`). A separate
LD_PRELOAD library samples interrupted instruction addresses with SIGPROF in
the **unchanged original executables**. Three sampling intervals (997, 1499,
2503 microseconds), CPU-2-pinned runs, and alternating uninstrumented controls
give 58.45%, 60.50%, and 61.26% of process CPU samples inside native
`evaluate<false>` / its chunks. No samples are dropped. Median sampling overhead
is 6.09% native and 6.49% Verilator; sampled timings are not performance results.
This method measures CPU sample distribution, not hardware cache-miss or IPC
counters. Constructor/loading samples are retained rather than disguised as
simulation-only samples.

The source explains the result:

- The native adapter prepares a cycle with `eval(false)`; `step()` then invokes
  `evaluate<true>` and another `evaluate<false>`: three graph traversals.
- All 79 generated chunks are called in both template specializations. Making
  `Commit` a compile-time constant does not remove the unused next-state cones
  across noinline functions and split translation units.
- A transitive walk of the serialized graph finds that the only top outputs
  (`exit`, `commit0`, `commit1`) depend on **12 state-word nodes and zero
  combinational nodes**. There are zero nontransactional RAM-access validations
  required by settling. For this particular top, settling needs only output
  publication, not any generated chunk.

Re-emitting the frozen graph reproduces `model.h` and both original parts
byte-for-byte, verifying source-to-chunk mapping. The two hottest chunk pairs,
31 and 32, are entirely/almost entirely CSR logic, not matrix multiply. Sample
counts within mixed chunks are not presented as exact per-module costs.
Verilator samples are chiefly its active/NBA combinational and sequential
kernels; its original tracing/formatting remains enabled.

Two temporary causal experiments retain all original generated arithmetic,
RAM operations, state updates and callbacks:

| Variant | Native median work time | Verilator median in same comparison |
| --- | ---: | ---: |
| Unmodified native, fresh uninstrumented profile controls | 19.680596771 s | 7.824380856 s |
| Omit redundant preparation traversal only | 12.199389760 s | 8.042086856 s |
| Keep preparation, but publish state-only outputs in both settle passes | **5.501015832 s** | **8.031748184 s** |

Each diagnostic comparison uses three alternating trials. Both match every
one of **297,074 integer commits**, the checksum `0xd687fdc5`, and exactly
**396,778 total / 396,768 work cycles** on the unchanged 32x32 ELF. The full
publication-only experiment is 3.58x faster than the fresh native control and
1.46x faster than its matched original Verilator runs. All original chunk
source bodies remain unchanged; recompiling them against the diagnostic header
produces byte-identical object files. The work/commit `evaluate<true>` machine
code section is also byte-identical to the unmodified control. Only the
noncommitting schedule changes. Compilation time is excluded.

The original tiny ELF also matches all 21,285 integer commits and 46,264 cycles:
publication-only native 0.657922728 s versus original Verilator 0.791019499 s.
These experiments establish the target, not an automatic production speedup.
The output-only bypass must not be applied indiscriminately to other models.

**Production goal:** reuse the existing dependency order and compute separate
live roots for work/commit and output settling in `cpphdl_graph_native.h`.
Settling must retain combinational output dependencies, applicable RAM-read
validation and cached host outputs, while excluding next-state-only producers,
RAM writes and committing host effects. Generate only the live phase-specific
chunks, preserving the existing clock/reset and simultaneous-commit contracts.
For this CVA6 top the inferred settling schedule should be empty automatically.
No hdlcpp-specific bypass, new competing scheduler, LUT expansion or arithmetic
rewrite is needed. Validate state-only and combinational-output small fixtures,
RAM read/write and callback regressions, then repeat the full commit comparison.

Profiles, diagnostic source patches, graph-cone analysis and complete
comparison reports are retained under `/tmp/cva6-profile-next.RIrFPo`;
temporary model copies, object files and bulky traces are archived and cleaned.

Final validation: all 32 RAM backend/oracle runs, register-bank exclusion checks,
the complete C++ graph regression suite, and all 37 harness unit tests pass.
Temporary conversion outputs, object files and raw traces are cleaned after
verifying every one of 5,288 archived files against SHA256. The 80 MiB
`verified-reference.tar.gz` retains the complete generated model, original
source snapshot, correctness traces and final small reproducers for follow-up
profiling; the frozen native executable and compact reports remain available.

### Automatic phase liveness: production follow-up (2026-10-14)

The preceding publication-only experiment is now implemented automatically in
`include/cpphdl_graph_native.h`, not in the CVA6 adapter. The emitter filters
the existing validated dependency order using output and nontransactional
RAM-read-check roots, stopping at committed state and inputs. Liveness runs
after parallel lane cloning. Every generated chunk is phase-specialized;
work-only producers and scratch stores disappear from settling, and chunks
without settling roots are not called. Mixed chunks still compute their live
output/check dependencies. Scalar, chunked, split and parallel emission share
this rule. There is no additional flag or competing scheduler.

The full test re-emits the verified graph extracted from ordinary hdlcpp C++;
SV elaboration and the unchanged C++ frontend are not repeated. RAMs, math
operations, all 41,690 scheduled nodes and 928 state groups remain intact.
The official adapter is unchanged, including its preparation `eval(false)`
and post-commit settling. For this top, both settling calls now automatically
publish state-only ports without calling any of the 79 chunks. Disassembly
confirms that the settling specialization contains no calls. The work/commit
specialization and **all 79 committing chunk machine-code sections** are
byte-identical to the retained profiling control. The final emitter reproduces
the measured header and both parts byte-for-byte after the helper refactor.

Validation compares the unchanged 32x32 ELF with the unchanged original
Verilator executable: checksum `0xd687fdc5`, **396,778 total / 396,768 work
cycles**, and all **297,074 integer RVFI commits** match exactly. The original
tiny ELF also matches 46,264 total cycles and all 21,285 integer commits.
Commit comparison is separate from timing; native commit logging is disabled
in timed runs. Original Verilator tracing/formatting remains enabled, as in
the retained original benchmark; these are harness comparisons, not a claim
that native graph beats every Verilator configuration.

After the initial three-trial correctness/performance comparisons, five
additional CPU-2-pinned trials per binary rotate the order of the old native
model, diagnostic reference, production model and original Verilator:

| Same 32x32 workload | Median work time | Five-trial range |
| --- | ---: | ---: |
| Native graph before phase liveness | 20.816452321 s | 19.274–22.417 s |
| Retained diagnostic publication-only reference | 6.600456913 s | 5.840–7.623 s |
| **Production automatic phase liveness** | **6.928928972 s** | **6.392–7.219 s** |
| Original Verilator harness | 9.005269789 s | 7.958–9.313 s |

Production reduces native runtime by **66.71% (3.00x faster)** and is **1.30x
faster than this original Verilator harness**. Its median is 4.98% above the
diagnostic reference. Background builds run concurrently and timings vary;
all samples are retained, rather than selecting the fastest run or comparing
against the historical 5.501 s reference as though host conditions were fixed.
The initial three-trial production comparison gives 6.557096124 s native and
8.475413096 s Verilator. Tiny-ELF medians are 0.731861688 s and 0.777657757 s.
Compilation and ELF-loading time are excluded from reported work time.

The full guarded re-emission/build takes 63.323 s and peaks at 503,070,720 bytes
process-tree RSS. The measured production executable SHA256 is
`1708ae20332b54395d011fb3ece0b413ca9c8517645bd341f93285027c0d46ac`.
Reports, all five timing samples, build commands and frozen references are
under `/tmp/cva6-phase-schedule.FMe9lq`; bulky generated files, objects and
traces are archived with verified per-file SHA256 before cleanup.

Regression coverage includes 15 sanitized phase-root configurations (including
an unobserved nontransactional read validator), unchanged work-only scratch
slots, zero settling chunk calls for state-only outputs, split memory/DPI
fixtures, the complete ordinary-C++ graph suite, threaded and named-clock
scheduling, host overlap/rollback/copy tests, and all 37 CVA6 harness unit tests.
Small tests pass before the full model is rebuilt. The early lane-cloning
ordering mistake is caught and fixed by the new parallel phase fixture; it is
not retained as an alternative scheduler or a model-specific workaround.
