# Lambda-free lowering and small CVA6 replay: performance audit

## Outcome

**Verilator-comparable performance is not achieved.** None of the additional
performance experiments beats the frozen, older cpphdl L1 model by 10%.
They remain isolated and are not enabled in production. Even the apparent 11%
gain from field projection against the newer lambda-free model falls below the
threshold in a repeat comparison. Do not present that preliminary gain as a
retained optimization.

The retained changes fix lambda-free conversion correctness and restore native
optimizer visibility through the newly outlined helpers. They are not an
improvement over the old lambda-based executable: the corrected lambda-free
model is still about 12% slower than that frozen baseline.

Only the representative AXI bus replay is regenerated and run. There is no full
CVA6 regeneration or new full-CPU matmul performance claim. Generated files,
binaries, traces and profiling data are outside the cpphdl repository, under
`/home/me/cpphdl-validation-20260917/`.

## Acceptance gate and timing

Every accepted simulation checks all 4,052 output bits for all **46,264 cycles**
against the same captured matmul bus trace before starting its work timer.
Every timed run consumes its outputs and reports checksum
`0261761b2a1c459e`. Compilation, model construction, trace preparation and the
reference checker are outside the reported work time. Timings are serial,
pinned to CPU 2, with alternating variant order. Cpphdl candidates use Clang `-O2`;
the original Verilator executable is unchanged.

Final repeat comparison, three trials per variant:

| Variant | Median work time | Individual trials, seconds |
| --- | ---: | --- |
| Original-SystemVerilog Verilator | **0.171884 s** | 0.174251 / 0.171884 / 0.170088 |
| Frozen older cpphdl L1 | **7.107423 s** | 7.168268 / 7.102044 / 7.107423 |
| Corrected lambda-free cpphdl L1 | **7.935810 s** | 7.949070 / 7.935810 / 7.934502 |
| Select fields before reconstructing packets | 7.216609 s | 7.095272 / 7.216609 / 7.269263 |
| Also bypass packed-array element proxies | 7.189853 s | 7.189853 / 7.127662 / 7.317254 |

The production lambda-free path is **46.2×** slower than Verilator here. The old
baseline is **41.4×** slower. The last two rows are rejected experiments, not
new production modes.

The earlier five-trial helper comparison gave medians of 0.162712 s for
Verilator, 7.119242 s for the old L1 executable, 14.275694 s for outlined helpers
without dependency expansion, and 7.916794 s after expansion. Thus dependency
visibility removes **44.5%** of the outlining regression, but does not establish
a speedup over the old flow. The final production cpphdl generator emits a
byte-identical bus model to this tested expansion prototype; the comparison
log is retained rather than silently substituting a different generated model.

## What is retained

- hdlcpp emits named runtime expression/array helpers and scoped packed-field
  updates in the exercised conversion paths. The regenerated bus headers
  contain no capturing `[&]` helper lambdas. Compile-time type selection
  and proof metadata are separate from runtime helper closures.
- Concatenation helpers receive explicit, ordered value/width arguments.
  An earlier attempt to hide module reads inside a helper introduced producer
  recursion and was replaced, not timed as a successful result.
- Packed-field projection recognizes the generated five-line scoped update as
  one operation. Without that recognition, the new spelling falls back to
  whole-packet evaluation and introduces false ready/valid dependencies. The
  regression failed at cycle 272; restoring the old demux isolated it, and the
  projector fix restores all 46,264 cycles.
- cpphdl qualifies calls to generated template helpers correctly outside their
  original class scope. L1 expands generated `__hdlcpp_expr_*` bodies before
  dependency/effect analysis, preserving parameter scopes, references and
  nested calls. Expansion has a recursion/depth guard.
- Helper metadata survives template reconciliation, shard merging and saved
  collections. New collections use v5; v4 remains readable. Native-only closure
  lowering is not emitted by hdlcpp into RTL-facing source.

Focused native O0/O2 tests and original-SV reference tests pass. New optimizer
tests cover nested helpers, two template instances, reference updates, local
names shadowing fields, and collection loading through a declaration-only seed.
Template-helper, indexed-write proof, repeatable-comb, lazy-cycle and work-mutation
regressions also pass. This is not a claim that the entire repository test suite
or the C++ to SV round trip passes; the older full `test_modules` run has a
separately verified preexisting `testTypeTemplateCastShiftKeepsTargetWidth`
expectation failure.

## Rejected experiments

All candidates below are external copies of tool sources or runtime headers.
There are no handwritten replacements for the AXI hardware, and none of these
candidate implementations was added to the production tool sources.

| Experiment | Checked result | Decision |
| --- | --- | --- |
| Addressable/native struct arrays using the existing hdlcpp configuration | Fails at cycle 0 | Reject; do not time mismatching output |
| Same arrays with strict same-type adapter fast paths | Still fails at cycle 0 | Reject; disproves the adapter-only explanation |
| Also change cross-array assignment from broadcast to packed conversion | Still fails at cycle 0 | Reject; not a demonstrated fix |
| Cache all direct combinational expressions | 7.978366 s median versus 8.112925 s parent | Reject: 1.7%, and broad caching is not a general purity proof |
| Word-sized packed extraction in runtime helpers | 9.316385 s versus 8.112925 s parent | Reject: slower, despite passing 258,045 independent bit-range checks under ASan/UBSan |
| Force generated struct pack/unpack methods to inline | 8.134376 s versus 8.112925 s parent | Reject: no gain |
| Emit logical field offsets and project the field before materializing the packet | Initially 7.205442 s versus 8.135609 s; repeat 7.216609 s versus 7.935810 s | Reject: repeat gain only 9.1%; no improvement over the older baseline |
| Read that field directly from the packed array, bypassing its element proxy | 7.189853 s versus 7.216609 s | Reject: only 0.4% incremental gain |

The field experiments are generated automatically by hdlcpp, without changing
the underlying RTL. They keep the same graph summary: 344 instances, 268 eager
values, 4,303 dynamic evaluators, 5,390 dynamic states and zero reported lazy
cycle back-edges. This isolates their representation change from adding a new
scheduler. Their small independent fixture covers nested fields, 65-bit data,
79-bit unaligned elements, native O0/O2, optimized L1 O0/O2 and original-SV
Verilator. Direct field-read bounds tests also compare the native fast path and
the fallback compiled as C++ with `-DSYNTHESIS` under ASan/UBSan, including zero
extension from narrow elements and truncation from wider elements. That fallback
check is not a C++ to SV round-trip test.

## Profiling and interpretation

PC sampling starts on the driver's first `CLOCK_MONOTONIC` call and stops on its
second. Each profiled run verifies exactly two such calls and passes the full
trace. These instrumented runs are not substituted for uninstrumented timings.

| Exclusive sample category | Outlined helpers, 14,544 samples | Expanded helpers, 8,177 samples |
| --- | ---: | ---: |
| Original request-node getters (`req_nodes_comb_func`) | 24.04% | 0 observed |
| Packed-array access | 12.75% | 15.14% |
| Struct pack/assignment | 12.97% | 17.87% |
| Logic slice/read/write helpers | 9.47% | 14.05% |
| Generated evaluators | 25.74% | 45.31% |
| Other/unresolved | 15.01% | 7.63% |

The dependency-visibility regression is measured, not inferred from naming:
outlined helpers call original producer methods outside the L1 graph; expansion
removes those observed getter samples and almost halves runtime. Later C++
inlining alone cannot repair an HDL dependency graph that never saw the reads.

After that repair, named packed transport accounts for 47.1% of samples.
Generated evaluators can contain more inlined transport, so these are exclusive
symbol categories, not a complete semantic attribution. Conversely, that table
does **not** prove that faster copy loops will improve total time: the word-copy
and forced-inlining controls explicitly fail that hypothesis.

Inspection of the actual Verilator-generated bus model shows shared native
word operations and direct input-word slices. Our generated model still has
thousands of demand evaluators and materialized packet/array boundaries. Its
executable text is 3,443,131 bytes versus Verilator's 317,527 bytes. This is a
code-size observation, not evidence of instruction-cache misses.

**Architectural inference, not a completed solution:** the useful next
abstraction is a hardware graph of logical bit regions and producer values,
with field selection, packing cancellation, aliases and elaboration resolved
before native C++ emission. Replacing the packed-object/demand-call backend with
that graph is materially different from renaming helpers, copying bytes faster,
forcing inlining, or adding another scheduler. Local field extraction alone
does not validate a route to Verilator speed; the repeat results explicitly
rule out claiming that it does. A future implementation must demonstrate the
whole replay gain, not just a faster isolated field accessor.

## Reproduction and evidence

Artifact root: `/home/me/cpphdl-validation-20260917/`.

- `no-helper-lambdas/compare.py` and `no-helper-lambdas/comparison/`:
  five-trial helper audit.
- `no-helper-lambdas/compare-candidates.py`, `final-small-comparison/`,
  `field-comparison/`, `field-slice-comparison/`: commands, binary/trace SHA-256
  hashes, every checked run, individual times and medians. `--field` and
  `--slice` select the later comparison groups.
- `no-helper-lambdas/WorkProfile.c`, `analyze-profile.py`,
  `no-helper-lambdas-projected/*profile*` and
  `helper-inline-experiment/work-profile*`: timed-region sampling evidence.
- `helper-inline-experiment/production-generate.log` and the empty
  `production-compare.log`: final production generator matches the tested model.
- `addressable-experiment/`, `addressable-strict-experiment/`,
  `expression-cache-experiment/`, `word-extract-experiment/`,
  `inline-pack-experiment/`, `field-projection-experiment/` and
  `field-slice-experiment/`: isolated rejected source variants, rebuild recipes,
  unit checks and replay logs. Their presence is audit evidence, not activation
  of a production optimization.
