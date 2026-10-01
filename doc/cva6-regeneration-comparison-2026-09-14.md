# Full CVA6 regeneration and matmul comparison — 2026-09-14

## Result

Fresh SystemVerilog → hdlcpp → cpphdl regeneration and compilation pass the
strict correctness comparison against the original Verilator CVA6 executable.
Cpphdl remains **9.76× slower** for this equivalent workload.

| Measurement | Original Verilator | Regenerated cpphdl |
| --- | ---: | ---: |
| Median simulation-loop time, five trials | 3.556401 s | 34.708700 s |
| Simulation-loop time range | 3.519066–3.572574 s | 34.044391–35.362387 s |
| Median whole-process wall time | 3.894080 s | 34.723407 s |
| Reset cycles | 10 | 10 |
| Work cycles | 46,254 | 46,254 |
| Total simulated cycles | 46,264 | 46,264 |
| Retired instructions, separate traced runs | 21,285 | 21,285 |
| Matmul check values | 16, 24, 28, 42 | 16, 24, 28, 42 |
| Guest result | PASSED | PASSED |

All 21,285 ordered PC/instruction pairs agree. Destination registers and write
data also agree at every retirement. Retirement cycle labels agree after the
documented observation-phase alignment: native combinational commit plus one
cycle versus cpphdl's registered RVFI observation. Unobserved execution finishes
at the same total cycle count.

## Regeneration

- Built both tools from the current checkout, then froze their executables and
  runtime headers for the run.
- Used a new model directory: 284 conversion sources, 287 generated files,
  zero conversion failures.
- Ran the L1 combinational optimizer: 958 instances, 753 scheduled values,
  11,176 dynamic evaluators, 12,477 dynamic states, 265 lazy-cycle backedges.
- Freshly compiled all 106 linked objects. No handwritten hardware replacement,
  stale generated model, or previous compiled model objects were used.
- Tool/runtime and source hash checks pass. The comparator's seven regression
  tests pass.

## Timing method

Five alternating trials per simulator follow a separate equivalence-validation
pair. Both simulators use the same integer-matmul ELF and termination condition;
the native command receives its ELF tracer plusarg after the program argument.
Every validation and timed execution passes and completes the same number of
cycles. Timed runs disable the optional diagnostic probes.

Simulation-loop time uses each harness's steady clock, excluding conversion,
compilation, initialization, ELF loading, and reset. Whole-process wall time is
reported separately. Both processes are pinned to CPU 2. The machine is shared:
unrelated background workloads remain active, so these are not isolated-host
benchmark results. Our conversion and compilation finish before timing begins.

Individual simulation-loop times in seconds:

| Trial | Verilator | cpphdl |
| --- | ---: | ---: |
| 1 | 3.556401047 | 34.708700383 |
| 2 | 3.560696327 | 34.798172092 |
| 3 | 3.546336057 | 35.362386818 |
| 4 | 3.519065572 | 34.516366556 |
| 5 | 3.572573729 | 34.044390996 |

## Reproduction and artifacts

Artifacts are under `build/regeneration-comparison-20260914/`:

- `run.sh`, conversion/optimization/build logs, and frozen tools/runtime.
- `check_correctness.py`, `correctness.json`, and reference/converted traces.
- `performance/comparison.json`: all commands, hashes, timings, and cycle counts.
- `performance/*/run.log`: separate logs for every measured execution.
- `environment.log`, `timing-host.log`, and source/hash checks.

Rerun the equivalent-work timing comparison from the cpphdl checkout:

```sh
root="$PWD/build/regeneration-comparison-20260914"
python3 hdlcpp/tests/cva6/compare_matmul.py \
  --verilator "$root/verilator-reference" \
  --cpphdl "$root/model/run_cpphdl_testharness_opt" \
  --output "$root/performance-repeat" --cpu 2 --trials 5
```

SHA-256:

- ELF: `a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c`
- Native executable: `cb11ead70712c9d333238904c9ae0535580b00e74dce450ec1b1f19a22f0b763`
- Regenerated executable: `790b365321d1114679b9eddc5713d6fa2d9b36aa41e06298d7a4294de76d566d`

Older generated sources and compiled caches are losslessly archived within
their original candidate directories to accommodate disk limits. The current
model's generated sources, objects, and executable remain available; its PCH
caches are gzip-archived after use and verified before removing the originals.
