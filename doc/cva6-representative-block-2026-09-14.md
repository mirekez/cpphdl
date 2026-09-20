# A representative small CVA6 performance test

**Superseded representativeness claim:** the
[follow-up coverage audit](cva6-replay-coverage-2026-09-14.md) measures one RF
decoder evaluation per epoch here versus 64 inside full CVA6, plus major missing
bus and CSR/RVFI paths. This remains a useful functional/packed-store regression,
not a full-core performance proxy. The measurements below are historical.

Follow-up: [causal profiling audit](cva6-regfile-profile-audit-2026-09-14.md)
checks the register-file diagnosis with timing interventions, exact evaluation
counts, assembly, and compiler remarks. It distinguishes this block's missed
nested-store lowering from the separate complete-arbiter recomputation problem.

## Result

The old request-only arbiter test omitted the expensive storage and consumer
patterns in CVA6. It is useful as a lowering unit test, but its near-Verilator
speed did not establish that complete CVA6 combinational logic was efficient.

The new recommended small test is the **original integer register file, replaying
actual matmul inputs captured from the original full-CVA6 Verilator executable**.
It contains two module instances, versus 958 in the full conversion. Both small
simulators match all **46,264 captured pairs of read values**, including reset.

| Benchmark | Cycles per timed run | Verilator median | cpphdl L1 median | cpphdl / Verilator |
| --- | ---: | ---: | ---: | ---: |
| Full CVA6 matmul, retained validated baseline | 46,264 | 3.556401 s | 34.708700 s | 9.76× |
| Register-file matmul replay, final runner | 2,313,200 | 0.225305 s | 1.773788 s | **7.87×** |

The small run is 50 complete replays, not 50 extra hardware instances or
artificial delay. Its gap is about 19% smaller than the full-core gap: a useful
similar-order reproducer, **not an exact 9.76× calibration**. Crucially, it also
reproduces the same packed-array helper bottleneck, rather than merely obtaining
a similar ratio by adding unrelated work.

The alternative `--optimize-combs` mode takes 1.765916 s (7.84×). Switching between
the two existing schedulers does not solve this case. No production optimizer or
runtime change was made in this task; the improvement is to the benchmark and
diagnosis, not yet to full-core throughput.

## Two concrete gaps in the current optimization

### 1. Native bit stores exclude the nested packed-array shape

`Combs.cpp:3399`, `lowerPackedBitStores`, recognizes a direct field followed by
one index. It explicitly excludes indexed storage at `Combs.cpp:3437` and expects
an assignment immediately after that index. The request-only test exercises:

```cpp
req_nodes_comb[bit] = value;
```

The integer register file actually emits:

```cpp
we_dec_comb[write_port][register_number] = value;
```

That is a packed array element followed by a bit selection. It is **not lowered**
by that pass. The generated code retains 2 × 32 assignments through packed-array
proxies and parent writeback, despite the final decoder fitting in 64 bits.

This is not an inference from benchmark ratios: the same nested assignment is
present in the full model's evaluator 1457 and the small model's scheduled comb.
Both profiles enter exactly the same
`array_packed_ref<logic<32>, 64, 32>::bits` helper. The old scalar request tree
never exercised that path.

### 2. Cheaper producers are still repeatedly recomputed

`Combs.cpp:3332`, `memoizableProceduralComb`, requires the first result access to
be a complete assignment before accepting a procedural producer for caching.
Complete results assembled through indexed writes can fail that test. This
conservative behavior preserves semantics, but does not prove complete writes
through the converted generate loops.

In a complete 11-input R arbiter, compiler function-entry instrumentation counted
**15,628,353 request-tree evaluations in 30,000 epochs: 520.945 per epoch**.
The grant tree ran 11.672 times per epoch. These counts include the 20,000-cycle
checker and a 10,000-cycle run; instrumented timings are not performance results.
The original request-only benchmark evaluates one producer once per iteration.
Making an individual bit store cheap cannot remove this work amplification.

The full-model profile independently identifies four outer-crossbar B/R
request-tree evaluators—6151, 6162, 10756, 10885—accounting together for about
9.8% of samples. Their emitted bodies lack a once-per-epoch entry guard. The
520.945 count is for the small complete arbiter, **not a measured call count for
each full-model arbiter**.

## Profiles: representation cost, not just scheduler overhead

Fresh SIGPROF PC samples were collected from the retained correct full binaries.
For the stripped cpphdl executable, a link map was generated from its existing
objects; the map-producing link was verified byte-identical to the baseline.

| Self-time sample category | Full cpphdl | Small register-file replay |
| --- | ---: | ---: |
| Slice writeback helpers | 29.37% | 18.40% |
| Slice read helpers | 10.17% | included in generated bodies / array helper |
| Array helpers | 5.52% | 22.59% |
| Generated dynamic combinational bodies | 34.67% | 0.61% |
| Generated scheduled comb body | included in other categories | 46.02% |

Approximately **45%** of full-model samples and **41%** of small-model samples
are in the slice/array helper categories. The full profile has 34,831 samples;
the small one has 1,647. Samples are whole-process, including initialization and
checking where applicable; these are approximate self-time shares, not call
graphs or timed-loop-only percentages. About 6.7% of full cpphdl samples fall
outside its executable. Generated comb-body time includes useful RTL work and
recomputation; it must not be labeled entirely as scheduler overhead.

Native Verilator's full profile also explains why a block ratio cannot simply
be extrapolated: `act_comb__TOP__29` and `act_comb__TOP__30` account for about
59% of whole-process samples in wide outer-crossbar request-copy logic. Their
integration context is absent from the request-only test. Native startup also
appears in these samples; it is excluded from the reported simulation-loop time.

## Experiments and choice of reproducer

- **Request-only control:** retained and rechecked exhaustively for 11/16 inputs,
  both scheduling modes, and three previous output values. It still tests only
  scalar request-tree lowering, without payload, arbitration state, or consumers.
- **Complete arbiter:** added structured 103-bit payloads, all outputs, fairness,
  internal priority, lock-in and backpressure. With production conversion
  metadata and matched Clang `-O2`, its preliminary three-trial medians were
  0.022879 s native and 6.077846 s L1 for 200,000 cycles, about 266×. It is retained
  as a fanout stress test, **not** as a full-core ratio predictor. A separate
  profile attributes 73% of its samples to the request-tree body.
- **Outer crossbar:** tested a 331-instance slice using the full conversion's
  headers/metadata and L1. All 4,096 reference cycles and timed checksums matched
  after correcting asynchronous-reset observation in the driver. It was still
  too large and sensitive to changed Verilator integration context for the
  requested minimal reproducer. Exploratory sources/builds are archived under
  `build/representative-block-20260914/xbar-fixtures/`; this candidate is not added
  to the maintained test flow. Its preliminary overlapping-run timings are not
  used as benchmark results.
- **Complete integer register file:** deterministic random traffic established
  differential correctness, then actual matmul traffic replaced synthetic
  activity for the recommended performance run. The configuration matches the
  full integer instance: 32-bit data, two read ports, two commit ports, x0 fixed
  to zero. Only `NrCommitPorts` is consumed from its configuration structure.

An earlier matmul-replay runner measured 8.47×; the final runner after input
validation hardening measures 7.87×. Both results are retained, not selected to
force the original full-core ratio. This small block represents the packed-store
problem well, but does not reproduce the full scheduler's 265 backedges, caches,
or instruction execution. The arbiter stress test remains necessary for work on
producer sharing.

## Measurement and validation

- Final benchmark: five alternating trials, CPU 2, matched Clang `-O2` for both
  small models and their drivers. Verilator's makefile optimization defaults are
  explicitly overridden. Compilation finishes before timing; unrelated shared
  host workloads remain active. Full-core numbers are the previously validated
  original-binary comparison, not a newly claimed full-core speedup.
- Final ranges: native 0.223269–0.226052 s; L1 1.760757–1.784184 s.
- Capture relinks unchanged original model objects with a read-only observer;
  no CVA6 behavior is replaced. Matmul passes, and repeated captures are
  byte-identical: 46,264 records, ten reset cycles. The observer is specific to
  the retained RV32 Verilator signal layout; its bindings must be reviewed for a
  different elaboration.
- Each replay checker compares every captured read pair against full CVA6,
  then against the standalone native reference. Every timed variant reports
  2,313,200 cycles and checksum `33cd39c93d03a8bc`.
- Parsing, trace preparation, reference checking and file I/O are outside the
  timer. Small timed runs include the ten reset cycles per replay; the full
  harness's work timer excludes reset. This tiny accounting difference is
  documented rather than hidden.
- Production-metadata register-file smoke also passes all 46,264 reads; its
  single-pass timed checksum is `a57c0e15b372e44d` in all three variants.
- Nine negative checks reject truncated traces, invalid addresses and corrupted
  expected values before timing. Thirteen Python unit tests pass, including six
  new trace-format tests and seven existing full-comparison tests.

## Use it for the next compiler change

See `hdlcpp/tests/cva6/automatic_request_tree/README.md` for capture and replay
commands. The maintained entry points are `capture_regfile.py` and
`check_request_tree.py --regfile --regfile-trace ...`; the latter regenerates
original RTL through hdlcpp and tests both cpphdl scheduling modes.

The next targeted work is **normalizing nested packed writes to native storage
operations**, then proving complete producer writes and sharing their evaluation.
Another scheduler or a request-only LUT would leave these measured costs in
place. Use the register-file trace to preserve output/cycle correctness and the
complete-arbiter test to detect recomputation; confirm any improvement on full
CVA6 before claiming a processor speedup. The isolated register-file decoder is
only about 2.9% of full-model self-time, so speeding that decoder alone cannot
deliver a 10× processor improvement.

Artifacts: `build/representative-block-20260914/`, especially `regfile-final/`,
`capture-final/`, `regfile-profile.json`, `cpphdl-profile.json`,
`verilator-profile.json`, `count-run.log`, and `negative-tests.json`.
The capture SHA-256 is
`49f834d155d48f31fb1d93dcf2e0ba780117a42936a2966d16c6028f8cc7f139`.
