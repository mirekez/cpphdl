# What the CVA6 register-file replay leaves out

## Conclusion

The current replay is a **functional and packed-store regression**, not a
representative full-CVA6 performance test. Capturing correct input values did
not preserve the compiler's dependency graph, evaluation multiplicity, or the
rest of the simulated design. The earlier similar slowdown ratios were not
enough evidence to call it representative.

The most important missing property is **demand scheduling with real consumers**.
The same converted integer register file runs its decoder once per epoch in
the replay, but **64 times per active epoch inside full CVA6**. Using the same
`--optimize-combs-l1` option does not make these execution graphs equivalent.

No production optimization or replacement hardware is introduced in this audit.
The full-core speed remains the previously validated 32.930 s versus 3.474 s
native for 46,264 cycles. Profiling/instrumented runs below are not speed claims.

## 1. Verified evaluation counts, not an inference from timings

Counters were inserted at the entry of five generated full-model evaluators.
All other generated behavior was retained. The instrumented model passes
matmul at 46,264 cycles and matches all **21,285 cycle/PC/instruction/destination/
value retirement records** from the validated fixed model.

| Full-model producer | Calls observed | Calls per active epoch |
| --- | ---: | ---: |
| Integer RF write decoder, evaluator 1457 | 2,960,064 | **64**, in every one of 46,251 active epochs |
| Outer xbar demux 0 B request tree, 6151 | 21,455,485 | 463 or 561 |
| Outer xbar demux 1 B request tree, 6162 | 21,420,121 | 463 or 477 |
| Outer xbar demux 0 R request tree, 10756 | 22,477,149 | 477 or 561 |
| Outer xbar demux 1 R request tree, 10885 | 22,061,937 | 477 |

Arbiter totals include 14 startup/reset epochs with 1 or 15 calls, and an initial
evaluation: 46,265 observed epochs, not 46,265 simulated cycles. Decoder-active
epochs must not be confused with the harness's 46,254 timed work cycles.
The full histograms are retained in `build/replay-coverage-20260914/count/run.log`.

The small replay was independently instrumented at its scheduled decoder body.
It executes **exactly once in each of 92,548 epochs**: one 46,264-cycle checker,
one 46,264-cycle timed replay, plus their asynchronous-reset settling passes.
Every captured read pair and the timed checksum `a57c0e15b372e44d` match native.
Instrumented times are deliberately excluded from comparisons.

The generated code explains these measurements:

- Small model: `calc_all` schedules the decoder before sequential work; the
  2-write-port × 32-register loop reads `we_dec_comb` directly.
- Full model: that same loop calls evaluator 1457 for **each bit it reads**.
  The decoder is demand-evaluated and lacks a once-per-epoch result guard.
- `Combs.cpp`, `identifyDynamicNodes`, propagates demand scheduling from cyclic,
  conditional, unresolved-call and work-order dependencies to their consumers.
  `memoizableProceduralComb` does not prove that this indexed decoder completely
  writes its result. The exact upstream seed making the full RF dynamic was
  not isolated in this audit; it must not be guessed from the count alone.

Therefore, adding more random RF values or repetitions cannot reproduce the
missing compiler behavior. Nor should the driver manually call the decoder
64 times: the **generated producer/consumer dependency must cause the calls**.

## 2. Fresh profiles of the fixed full model

Two new PC-sampling runs of each original native and fixed cpphdl executable
pass matmul at 46,264 cycles. The fixed cpphdl link map was rebuilt from its
actual objects; the map-producing executable is byte-identical to the measured
fixed binary (SHA-256
`e075475a14c04b436f7ceb12068cc284cee96a5699a58d5173b6250de6469692`).

| Exclusive PC sample category | Fixed cpphdl run 1 | Run 2 |
| --- | ---: | ---: |
| Packed slice writeback | 30.52% | 30.67% |
| `logic::bits` slice reads | 10.56% | 10.81% |
| Four outer-xbar request-tree bodies | 9.93% | 9.68% |
| Integer RF decoder body | 1.37% | 1.45% |

These are **self-time**, not inclusive module costs. The RF still incurs other
helper/consumer work; its self-time is not a bound on all possible RF savings.
Counts are 26,697 and 28,730 samples. Sampling includes initialization, unlike
the full harness work timer.

The native reference has a different dominant cost: two generated outer-xbar
wide request-copy functions, `act_comb__TOP__29` and `act_comb__TOP__30`, account
for **57.97% and 59.08%** of its two profiles. Their source manipulates the
4,114-bit per-demux request bundles. A standalone arbiter that removes this
integration work changes the native denominator as well as cpphdl's work.

### Caller attribution changes which wide blocks we should add

A separate stack-sampling run passes matmul and collects 31,336 samples.
The interrupted PC is recovered in every sample; 29,929 recover at least one
caller. Chains without a recovered generated caller remain unattributed.
This is an additional diagnostic, not a replacement for the two PC profiles.

- **512-bit slice reads are not evidence for a cache-line replay.** All 1,196
  sampled const `logic<512>::bits` calls resolve to CSR/RVFI evaluators.
  Generated CSR code includes packing/unpacking the 64-entry PMP configuration
  array; RVFI repeatedly consumes packed CSR fields.
- All 1,197 sampled `logic_bits<3562>::updateParent` calls resolve to CSR/RVFI
  paths. The 3,562-bit CSR probe structure must be included with its consumers,
  not replaced by a prepacked constant.
- All 595 sampled 480-bit writebacks occur in the RVFI/tracing work partition.
  634-bit probe-field writebacks also occur in RVFI evaluation.
- Of 1,257 sampled 136-bit writebacks, 776 come through the HPDcache replay
  table's address producer (16072), and 332 through outer-xbar paths. Width
  alone does not identify a module.
- All 433 sampled const 2,168-bit slice reads come through evaluator 13809,
  transporting scoreboard entries into issue-stage forwarding. The replay
  lacks this packed-entry producer/consumer path.

These observations support **multiple missing connected cones**, not just
making the register-file array larger. Detailed caller lists and unknown
attribution are in `build/replay-coverage-20260914/stack-summary.json`.

## 3. What to add to the replay

### First: preserve the compilation boundary, not only signal values

Extract original converted blocks using the full elaboration/collection data.
Retain their consumer fanout and the dependencies which cause demand scheduling,
including applicable conditional, retained-value and work-order constraints.
At a cut boundary, preserve or explicitly describe those constraints rather
than silently replacing every producer with an eager scalar input.

Record the actual block-local reset, input-update/observation phase, and all
required boundary fields over the same matmul cycle sequence. Compare outputs
before timing. The existing RF trace can remain a fast correctness regression;
it is not a complete scheduling-context specification.

### Then add original RTL cones in this order

1. **Outer AXI crossbar integration:** `axi_xbar_intf` / `axi_xbar`, both slave
   demuxes, their four 11-input B/R arbiters, downstream muxes and interface
   packing/unpacking. Preserve the 2×10 topology, payload widths, actual address
   map, arbitration state, ready/valid feedback and all sidebands. Capture real
   matmul boundary traffic, including idle-cycle values. The archived bare-xbar
   fixture uses synthetic traffic and a synthetic address map; it is not ready
   to stand in for this cone.
2. **CSR-to-RVFI observation:** CSR/PMP field production → `cva6_rvfi_probes` →
   `cva6_rvfi`, together with the observation consumers/state that force the
   packed conversions. Include 512-, 3,562-, 634- and 480-bit paths. This work
   exists in the simulated harness even when terminal output is just matmul.
3. **Issue/scoreboard/RF integration and HPDcache replay-table/memory-control
   paths:** retain forwarding-entry copies, indexed structured storage, and
   the original consumers of address/request producers. These cover the
   independently observed 2,168-/271-, 136- and 176-bit operations.

Start with the bus and observation cones rather than a handwritten replacement
or a synthetic weighted delay. They explain substantial costs on **both** sides
of the comparison. A single two-instance RF cannot cover these mechanisms.

### Acceptance criteria

- Same captured cycles and equivalent boundary outputs; all timed outputs stay
  live. Trace parsing, input preparation and validation stay outside timers.
- Same original RTL, production parameters and automatic hdlcpp/cpphdl flow.
- Check emitted eager/demand classification **and measured evaluations per
  epoch**, not merely optimizer flags or module counts.
- Compare full and partial profiles by producer/helper/caller, then check that
  the same compiler change improves the corresponding cost in both.
- Do not accept a matching ~9.5× slowdown as proof of representativeness. Keep
  the full-CVA6 correctness/performance run as the final integration gate.

## 4. Boundary experiments rejected during this audit

Two inexpensive attempts checked whether changing only the driver would suffice:

- Moving two trace-input copies into the root's `_work` fails the captured
  read comparison at cycle 282: the decoder still runs before those writes.
  **Rejected; no performance result used.** This fixture is not a valid replay
  and no production change is made to accommodate it here.
- Adding ordinary getter methods around the two inputs remains eagerly
  scheduled after optimization. All 46,264 read pairs pass, but five alternating
  trials remain essentially unchanged: fixed direct 0.02994 s, getter 0.02940 s
  for 231,320 cycles; before-fix direct 0.17709 s, getter 0.17757 s; native
  0.02330 s. **Rejected as a way to preserve full-model demand context.**

Neither experiment is promoted into the maintained test. In particular, names
such as `demand-fixed` in archived experiment logs are hypotheses, not evidence
that the generated schedule actually became demand-driven.

## Artifacts and scope

Audit artifacts are under `build/replay-coverage-20260914/`: `profile.py`,
`cpphdl.map.gz`, the four PC sample runs, `count.py`, `counts.json`,
`count/run.log`, `small-count/run.log`, `stack_sample.c`, `stacks/`,
`summarize_stacks.py`, `stack-summary.json`, and the rejected boundary controls.
Counters modify only scratch generated translation units; original binaries
and production tool sources are unchanged by this audit. Samples/counters are
not used as uninstrumented performance measurements. Successful timed controls
run serially on CPU 2 after compilation, with the existing unrelated host load.

**Status:** the missing mechanisms are identified and measured. A new combined,
context-preserving replay has **not** yet been implemented or validated.
