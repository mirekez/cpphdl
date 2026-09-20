# CVA6 conversion correctness: control dependencies and procedural arrays

Performance comparison is intentionally disabled until workload output and
simulated cycle counts agree. This investigation uses the original integer
matmul ELF and generated hardware; it introduces no CVA6 hardware replacement.

## Reproduced defect

The preceding full conversion retired six boot-ROM instructions and then stalled
on the first DRAM instruction fetch at `0x80000000`.

Read-only stored-value probes and instrumentation of the existing generated
evaluators identified a false combinational cycle at cycle 306:

1. The AMO adapter starts producing its upstream read-valid signal.
2. It requests the downstream read-valid signal through LR/SC and the delayer.
3. The generated AXI-to-memory read-valid producer writes valid, then evaluates
   `r_ready` even though that branch no longer writes anything in this slice.
4. Ready arbitration recursively requests the upstream read-valid producer.
   Its wrapper caches the producer's temporary zero before the producer finishes.
5. The memory adapter has a valid response, but the upstream cached valid is zero.

The original SystemVerilog read-valid assignment has no such ready dependency.
The converter retained a nested switch containing only labels and `break`s after
removing its unrelated counter assignments. Those breaks kept the enclosing
`if (r_ready)` alive.

Evidence is retained in `build/fixed-equivalence-20260913/diagnostic/`:
`raw-state.log`, `instrumented.log`, `raw.cpp`, and `dynamic_8.cpp`.
The diagnostic executable does not replace any hardware behavior; it logs
stored values and the original evaluator entry order.

## Tool fix

`hdlcpp/hdlcpp_comb.h` now distinguishes a switch shell from retained work in
all three combinational slicing paths: target, projected field, and projected
array field. A switch whose cases contain only terminating breaks disappears,
allowing the enclosing irrelevant condition to disappear too.

When any case retains work, every label and break remains, including empty
cases that must suppress a nonempty default. Inline work and loop `continue`
statements are conservatively considered relevant.

## Validation

- Combinational extraction tests pass with assertions enabled, including scalar,
  field, array-field, grouped-label, and nonempty-default coverage.
- The same tests pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- A SystemVerilog conversion regression reproduces the unwanted ready dependency
  with the previous converter and passes with the fixed converter.
- Struct conversion tests pass.
- The broader module suite stops at the existing
  `testTypeTemplateCastShiftKeepsTargetWidth` text expectation. The same failure
  is reproduced separately using the previous frozen converter.
- Fresh full CVA6 conversion: 284 conversion sources, 287 generated files,
  seven trait-convergence passes, zero conversion failures.
- Fresh cpphdl optimization: 958 instances, 775 scheduled values, 11,154 dynamic
  evaluators, 12,609 dynamic states, 265 lazy-cycle backedges (previously 377).
  These are graph counts, not a performance claim or proof of full equivalence.

## Initial full workload result

The freshly generated and optimized full CVA6 model now completes the original
integer matmul program:

```
PASSED
*** SUCCESS *** (tohost = 0) after 46265 cycles
```

Both a commit-observed run and a second run with observation disabled complete
at **46,265 total simulated cycles** (10 reset and 46,255 work cycles). Neither
run enables the getter-based IRQ diagnostics. The observed run retires 21,285
instructions according to the commit-valid counters.

The executable's disassembly identifies the four result-check loads. Their
retired destination-register values confirm the arithmetic, not just the final
success string:

| Result | Check-load PC | Loaded value |
| --- | --- | --- |
| C00 | `0x8000311c` | 16 |
| C01 | `0x80003130` | 24 |
| C10 | `0x80003144` | 28 |
| C11 | `0x80003158` | 42 |

Artifacts are in `build/sliced-correctness-20260913/`:
`run/correctness.log`, `run-unobserved/correctness.log`, `correctness.json`,
`conversion.log`, `optimization.log`, `build-serial.log`, and `frozen-check.log`.
`run.sh` regenerates from SystemVerilog; `resume_build.sh` completes the build
serially after the first parallel attempt exceeded memory. All 100 linked
objects come from this fresh generated model, not an older conversion.

The ELF SHA-256 is
`a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c`.
The new simulator SHA-256 is
`35efe1436e28a71933cf59bcf9710e6ff29ab57278f3389aa81cdfa78b68287e`.

### Remaining equivalence limits

This validates the matmul result and fixes the reproduced conversion stall;
it is **not a cycle-equivalence claim**. The previous original-Verilator run
completed at 69,315 cycles, whereas this model completes at 46,265. No timing
comparison or speed ratio was run. Cycle alignment and host/debug handshakes
still need investigation before such a comparison is valid.

At this stage, the converted RVFI `insn` field did not consistently match the retired PC
(for example at the result-check loads). The table above
uses ELF disassembly for instruction identity and the recorded PC/rd/data, not
that instruction field. The subsequent repair is described below.

## Procedural array repair

The RVFI issue queue performs blocking writes to an array of instruction/valid
records. An element can be filled, moved, and invalidated within one combinational
process. Conversion incorrectly replaced intermediate field reads with getters
for the completed aggregate or completed sibling field. This corrupted the
instruction stream reported by RVFI, including zero instructions during boot.

The generic converter fix has three parts:

1. Preserve source aggregate member reads until their containing process is known.
2. Slice an array field first, then rewrite its own reads to its local projected
   storage, including parenthesized element copies. They must observe intermediate
   blocking writes rather than recursively calling the aggregate producer.
3. If that slice still needs another intermediate aggregate field, project the
   completed result of the original procedural producer instead. Ignore
   `decltype` references, which read types rather than values.

The check deliberately runs **after field slicing**. An experimental blanket
fallback for self-reading arrays reconnected independent ready/valid producers;
the full model stalled before its first retirement. That approach was replaced,
not retained. A narrower intermediate build also encountered disk exhaustion;
it is not a correctness or performance result. No hardware block was hand-written
or replaced.

### Validation of the retained fix

- The executable blocking-array regression fails with the preceding converter
  and passes with the repaired converter: plain C++ at O0/O2, and both cpphdl
  scheduler modes at O0/O2. It checks both getter orders over 64 input vectors.
- Combinational extraction tests and five focused module projection tests pass
  with assertions enabled. The array runtime and extraction tests also pass
  AddressSanitizer/UndefinedBehaviorSanitizer.
- Fresh full conversion: 284 conversion sources, 287 generated files, seven
  trait-convergence passes, zero failures.
- Frozen cpphdl optimization: 958 instances, 753 scheduled values, 11,073 dynamic
  evaluators, 12,516 dynamic states, 265 lazy-cycle backedges.
- All 105 linked objects are freshly compiled from this conversion.
- Observed and unobserved full runs both print `PASSED` and complete after
  **46,265 cycles**. All four result-check loads remain 16, 24, 28, and 42.
- All **21,285** converted PC/instruction pairs occur in the native RVFI trace's
  PC-to-instruction lookup. This validates instruction identity at those PCs;
  it does not assert identical ordered execution through the debugger.

Retained artifacts are in `build/array-projection-correctness-20260913/`, including
`run/correctness.log`, `run-unobserved/correctness.log`, `correctness.json`,
`check_correctness.py`, conversion/build logs, and tool/source hashes.
The simulator SHA-256 is
`2c61b270f89de8321696a2d0b8275a091e7ffbddbac91166d4c6ca961f24d364`.

### Corrected native setup and remaining timing difference

The previous native command did not initialize the RTL tracer's `tohost`
address. It waited for later DTM polling and finished at 69,315 cycles, whereas
cpphdl initialized that address from the ELF. With the existing native executable
given `+elf_file=PATH` after the binary argument, the native tracer uses the same
ELF-derived termination condition and finishes at **46,264 cycles**. Both models
now retire **21,285 instructions**. The comparison command builder is fixed and
covered by a regression test; the performance comparator itself was not run.

The remaining difference is real, not just a footer counting adjustment:

- Native `NATCOMMIT` samples CPU commit before the registered RVFI observation.
  Adding one to its cycle labels aligns the first 583 retirements exactly.
- The next retirement, the load at `0x8000319a`, is one cycle later in cpphdl.
- Read-only probes narrow this to the AXI read reaching the memory adapter:
  native AR-valid is asserted in idle at cycle 3554; cpphdl asserts it at 3555.
  The memory state transition and subsequent cache response are one cycle later.
- PC/rd/data match for the first 17,699 retirements. The next difference is a
  debug-ROM GO-flag load at `0x830`; ordered PC/instruction traces first differ
  after 17,702 matching retirements as the debugger takes a different path.

The AXI request timing discrepancy still needs a root-cause fix before claiming
cycle equivalence or publishing a speed ratio. Probe sources and logs are in
`build/array-projection-correctness-20260913/probe-run/` and
`build/trace-correctness-20260913/reference-probe/`. They only observe generated
hardware; the correctness executable contains none of this instrumentation.

RVFI `order` remains zero because the original `cva6_rvfi.sv` does not assign it.
It is not repaired by inventing a counter absent from the source RTL.

### Artifact storage

To stay within disk limits, the previous frozen PCHs and the older
`build/full-equivalence-20260912/model/run_cpphdl_testharness_opt` binary were
losslessly gzip-archived. The latter's decompressed SHA-256 was verified before
removing its uncompressed copy. The preceding fixed model's object/dependency
files were also archived to `build/fixed-equivalence-20260913/build-objects.tar.gz`
and checked using `tar --compare` before removal. No source or regression result
was discarded.

Later disk-pressure cleanup likewise uses lossless archives verified against the
original files before removal. Earlier model sources are in each candidate's
`model-sources.tar.gz`; compiled caches are in `compiled-cache.tar.gz`; older
executables and frozen cpphdl binaries use `.gz`. Restore archives into their
original model directories before attempting incremental builds. The retained
candidate's generated sources and executable remain directly available.

## Packed array bitstream repair (2026-09-14)

Read-only probes traced the delayed AXI request upstream to replay-table
allocation. At cycle 3549, the converted allocation mask is 2 and its dependency
array correctly packs to `0x20000`, but the combined dependency-set array is zero.
At cycle 3550, native dependency state is `0x20040`; converted state is only
`0x40`. The write-buffer registered states still agree at this point.

Two generic representation errors account for this loss:

- The converter's whole-array assignment normalization used `sv_assign_field`
  for addressable array storage. Its unpacked-array semantics broadcast the
  scalar expression to every element instead of distributing its packed bits.
  Packed SystemVerilog arrays of structs also use this addressable C++ storage.
- The runtime's `unpack_value` delegated to the raw array storage assignment,
  advancing by `sizeof(element)` rather than the element's logical bit width.
  Its array `type_width` likewise included physical padding. Replay dependencies
  contain eleven one-bit fields, so these strides are very different.

The converter now explicitly unpacks whole-array expression results. The SV
runtime helpers recursively use logical array-element widths for width
calculation, packing, and unpacking, including register-backed arrays. Native
array storage layout and ordinary unpacked scalar-broadcast assignment remain
unchanged. No CVA6-specific replacement is introduced.

Validation:

- The new executable SV regression fails at vector 0 before either fix.
  Converter repair alone reaches vector 1, exposing the runtime stride error.
  Both fixes pass all 176 vectors at O0/O2, plain and both optimizer modes.
- Nested/register-backed 88-bit bitstream tests and existing field-assignment
  tests pass O0/O2 and AddressSanitizer/UndefinedBehaviorSanitizer.
- Existing ArrayPacked, ArrayUnpacked, and ConstexprHelpers runtime checks pass;
  the SV bitstream regression also passes both sanitizers.
- The preceding blocking-array regression still passes all six configurations.
- Fresh conversion completes with 284 sources, 287 generated files, zero
  failures. Optimization completes with 958 instances, 753 scheduled values,
  11,009 dynamic evaluators, 12,477 dynamic states, and 265 backedges.

All 106 linked objects were freshly compiled from the regenerated model.
The observed and unobserved converted runs both pass in **46,264 cycles**,
matching the original native Verilator executable. Both retire **21,285
instructions**. The strict comparison passes for every retirement:

- Ordered PC/instruction pairs match the native RVFI trace.
- PC, destination register, and write data match the native commit trace.
- Cycle labels match after the previously documented observation-phase
  alignment (native combinational commit plus one cycle versus registered RVFI).
- Result-check loads are **16, 24, 28, 42** in both executions.

This resolves the one-cycle discrepancy and the later debugger-path divergence;
it is not a change to the harness's cycle counter or termination criterion.

Artifacts: `build/packed-array-correctness-20260914/`, including
`correctness.json`, the assertion-based `check_correctness.py`, reference and
converted traces, regression logs, and source/tool hashes. Simulator SHA-256:
`dc98f0b2fcd986014e49ff446caf56a977acf5607debfe7e06c7c31b6fa0fc85`.
No performance comparison is run.

The preceding model's objects and internal PCH are preserved in its
`compiled-cache.tar.gz`, checked against the originals before removal; restore
into `model/build/opt/` using `compiled-cache-files.txt` and verify with
`compiled-cache.sha256`.

A subsequent clean regeneration and five-trial equivalent-work comparison is
recorded in [the regeneration report](cva6-regeneration-comparison-2026-09-14.md).
Both models again match at 46,264 cycles; median simulation-loop times are
3.556401 seconds for Verilator and 34.708700 seconds for cpphdl (9.76× slower).
