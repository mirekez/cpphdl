# Native graph cross-design benchmarks

`tests/graph/NativeGeneralBench.py` benchmarks native graph scheduling across
nine workloads. It constructs value graphs, emits native C++ at 1/2/4/6
requested inner lanes, compiles the models, and compares direct serial
evaluation with outer static/dynamic batching and inner graph threading.
The benchmark does not invoke Quartus or require an FPGA.

```sh
python3 tests/graph/NativeGeneralBench.py --cxx /usr/bin/g++ \
  --work /large/work/native-general --repeat 9
```

Use `--workloads control pipeline --quick --repeat 1` for a correctness smoke
test, `--build-only` to stop after compilation, or `--reuse-build` to rerun
existing executables. Benchmark artifacts, compiler logs, generated lane
plans and incremental `results.json` are written beneath `--work`.
Allow about 1 GiB of free memory for the largest workload, in addition to
compiler memory, and several minutes for compilation and measurement.

## Coverage

| Workload | Structure and purpose |
| --- | --- |
| `alu` | Eight independent 192-round 64-bit arithmetic datapaths per actor. |
| `shared` | A 768-round common cone feeding eight register cones; exposes duplicated fan-in. |
| `bram` | 1 MiB of mutable RAM per actor, dependent random reads and ordered same-address writes. |
| `large-ram` | 64 MiB per actor, 768 MiB in total; tests a large simulator working set. |
| `control` | Small conditional counters, shifts and multiplexers; exposes dispatch overhead. |
| `pipeline` | 64 clocked arithmetic stages with eight sampled outputs. |
| `dependent` | One actor with one long serial dependency chain; exposes limited parallelism. |
| `bus` | Actors sharing a host memory controller with one response per clock, variable 7–11-clock latency and backpressure. |
| `mixed` | Three arithmetic-heavy actors followed by nine small controllers; exposes uneven job costs. |

There are twelve actors per workload except `dependent`, which has one.
Outer batching evaluates the one-lane generated models. Inner threading
evaluates the multi-lane models sequentially across actors using one shared
executor. Requested and actual inner lane counts are recorded separately;
cone planning can retain fewer useful lanes than requested.
All models use 512-node chunks with chunk memoization disabled. Mutable RAM
does not support chunk memoization, so this keeps the mode comparison
consistent across the suite. These inner-threading results do not characterize
the separate adaptive dispatcher of memoized pure graphs.

These are graph-level workloads for the native evaluator. The RAM cases use
the graph's mutable memory semantics; their sizes describe host simulator
storage, not physical BRAM placement or an FPGA DRAM timing model. The bus
case explicitly models a shared controller and stalled responses, with its
host service work included in the timing. Existing mapped FP32 MAC gate
comparisons provide separate evidence for the retimed Boolean-netlist path.

## Measurement and correctness

Every simulated clock snapshots actor inputs, executes one step for every
actor, joins, and then consumes outputs. There is no multi-clock lookahead.
All scheduling modes use the same clock count within a workload. Construction,
initialization, full RAM digesting and reference evaluation are outside the
timed interval. Input preparation, controller service, state evaluation,
synchronization and output consumption are included.

Every measured run is verified against independent scalar C++ state equations.
The check covers a trace fingerprint of all published outputs, a digest of
every mutable RAM word, and the shared controller's transaction digest.
It also checks that scheduling variants and repeated identical traces agree.
This catches errors in state publication, memory write priority and bus
ordering as well as arithmetic differences. Reset and disabled updates occur
during every workload.

The runner calibrates a common cycle count, targeting a 350 ms direct run
while capping the estimated slowest variant near one second. Calibration is
an estimate rather than a hard runtime bound. When a variant is too cheap,
it repeats identical full traces until a sample contains at least 150 ms of
measured execution, subject to a 1,024-trace cap. The report retains subsample
counts and durations. Check the actual aggregate duration before interpreting
very small differences.

Nine repetitions rotate and reverse scheduling order. The report includes
median host nanoseconds per actor-clock, aggregate CPU usage, min/max timing,
coefficient of variation and per-round speedup ratios. Speedups compare
medians against direct evaluation in the same workload. Ratios above one
indicate faster simulation. The all-workload geometric mean includes the
limited-parallelism and small-control cases; selecting a different best
configuration for each row is not a single general scheduling policy.

## Interpretation

The executor is a general opt-in capability for independent jobs. Performance
depends on work per job, synchronization frequency, shared memory bandwidth,
load balance and available host CPUs. Small jobs can lose time to dispatch;
shared control work can limit scaling; inner cone splitting can duplicate
logic. Preserve those cases in published results rather than reporting only
successful arithmetic examples.

Results on one shared host do not establish universal speedups. Repeat on a
quiet host and on another CPU before promising a thread-count-specific
performance gain. Neither host CPU usage nor host simulation speedup measures
FPGA throughput, pipeline frequency or Llama tokens per simulated second.

## Recorded shared-host run

The 2026-10-17 run completed all 891 samples (nine workloads, eleven modes,
nine repetitions), comprising 9,298 verified full traces after cheap-sample
aggregation. Every aggregate sample contained at least 150 ms of measured
execution. Six CPUs were allowed; unrelated jobs and an existing Llama
simulation continued running. The hlsgen project retains the full summary,
per-sample timings, source/executable hashes and trace digests in
`doc/validation/native-general.json`.

| Workload | Static 2 | Static 4 | Static 6 | Dynamic 2 | Dynamic 6 | Inner 6 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Arithmetic | 1.41× | 0.88× | 0.80× | 1.42× | 0.85× | 0.07× |
| Shared fan-in | 1.62× | 0.73× | 0.56× | 1.56× | 0.62× | 0.55× |
| 1 MiB RAM | 1.54× | 1.61× | 1.60× | 1.90× | 1.69× | 0.30× |
| 64 MiB RAM per actor | 1.64× | 1.52× | 1.69× | 1.80× | 1.99× | 0.52× |
| Small control | 0.36× | 0.05× | 0.03× | 0.35× | 0.04× | 0.0015× |
| Pipeline | 1.19× | 0.39× | 0.33× | 1.33× | 0.35× | 0.02× |
| Serial dependency chain | 1.01× | 1.01× | 1.01× | 1.01× | 1.01× | 1.01× |
| Shared stalled bus | 0.42× | 0.04× | 0.03× | 0.30× | 0.03× | 0.0013× |
| Uneven blocks | 0.85× | 0.58× | 0.57× | 1.28× | 0.57× | 0.14× |
| **Geometric mean, all workloads** | **0.99×** | **0.44×** | **0.39×** | **1.04×** | **0.41×** | **0.07×** |

The two-worker arithmetic speedups ranged from 1.33–1.54× across repetitions,
and shared-fan-in speedups ranged from 1.42–1.88×. Both improved in every
repetition. RAM results varied more: two-worker ranges were 0.93–2.38× for
the 1 MiB case and 0.88–1.91× for the large case. These are observed
per-round ratios with rotated measurement order, not confidence intervals.

The result supports outer batching as a general opt-in mechanism for
sufficiently expensive independent blocks. It does not support enabling it
unconditionally. Static six-worker batching regressed the all-workload
geometric mean to 0.39×, and even the 1.04× dynamic two-worker average is too
small to establish a general gain on this shared host. Tiny jobs need serial
execution; uneven jobs can benefit from dynamic assignment; larger worker
counts need enough work to cover their synchronization cost. Automatic
admission and worker-count selection require separate implementation and
validation before a default-on improvement can be claimed.
