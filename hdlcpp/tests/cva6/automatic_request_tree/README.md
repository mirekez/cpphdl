# Automatic CVA6 block regressions

## Stateful packed-store regression: integer register file

The request-only tree is a lowering unit test, **not a CVA6 performance proxy**.
Use the complete integer register file for a small, stateful regression. It
converts the original `config_pkg.sv` and `ariane_regfile_ff.sv`, uses the RV32
configuration (32-bit words, two read/write ports, hardwired x0), and observes
both read ports on every cycle. No register-file implementation is handwritten.

```sh
python3 hdlcpp/tests/cva6/check_request_tree.py --regfile \
  --cva6-source /path/to/cva6 --output build/regfile \
  --verilator /path/to/verilator --cxx clang++ --opt-level O2 \
  --iterations 2313200 --trials 5 --cpu 2
```

Without a trace, it checks 20,000 deterministic randomized cycles before timing,
including competing writes and writes to x0. To reproduce **matmul activity**,
capture the retained original full-CVA6 Verilator build, then replay its inputs:

```sh
python3 hdlcpp/tests/cva6/capture_regfile.py \
  --native-build /path/to/cva6/work-ver \
  --verilator-root /path/to/verilator --riscv /path/to/riscv \
  --output build/regfile-capture
python3 hdlcpp/tests/cva6/check_request_tree.py --regfile \
  --regfile-trace build/regfile-capture/matmul-regfile.bin \
  --cva6-source /path/to/cva6 --output build/regfile-matmul \
  --verilator /path/to/verilator --cxx clang++ --opt-level O2 \
  --iterations 2313200 --trials 5 --cpu 2
```

The observer relinks **unchanged original objects** with an `eval_step` wrapper;
it does not recompile or replace CVA6. Its signal names and read-address bit
positions are specific to the retained RV32 Verilator build. A different
elaboration requires checking those observation bindings, not changing the DUT.
`capture.json` records commands and hashes. The trace is a little-endian stream
of 24-byte `RegfileTrace` records, starting with reset. Both small simulators must
match the full-CVA6 captured read values for **all 46,264 cycles**, then match one
another's reference outputs and timed checksum. 2,313,200 cycles are 50 complete
replays, including reset. File I/O, checking, model creation and conversion are
outside the timer; both timed runs consume the same in-memory input sequence.

The runner requires automatic nested packed-store lowering in both optimizer
modes; it fails if the decoder still uses the old unlowered path. This isolates
the packed-store bottleneck, not the entire processor's cost distribution or a
guaranteed whole-CVA6 speedup. See `doc/cva6-nested-packed-store-fix-2026-09-14.md`.

The follow-up `doc/cva6-replay-coverage-2026-09-14.md` measures another missing
property: this replay schedules the decoder once per epoch, whereas full CVA6
demand-evaluates it 64 times per active epoch. Matching captured values and using
the same L1 option does not preserve the full dependency graph. A representative
integration replay also needs the original bus, CSR/RVFI and scoreboard/cache
producer-consumer paths described there; do not use this RF ratio to predict
whole-core speedups.

## Complete arbiter stress test

Add `--complete-arbiter` instead of `--regfile` to exercise the original 11-input
R-channel arbiter: 103-bit structured payload, internal round-robin state,
fairness, lock-in, backpressure, and all external outputs. Input packing happens
before timing. This deliberately exposes consumer fanout and producer
recomputation that the request-only test misses; its much larger slowdown is
**not** advertised as a whole-CVA6 ratio prediction.

`--cva6-metadata /path/to/regenerated/model` optionally reuses production
conversion settings while converting the block RTL afresh. The default clears
conversion overrides and supplies module declarations through `ArbiterDesign.sv`.

### Comb-only diagnosis and regression (2026-10-25)

Use fresh conversion, not a previously generated request-only model:

```sh
python3 -B hdlcpp/tests/cva6/check_request_tree.py --complete-arbiter \
  --cva6-source /path/to/cva6 --output /tmp/cva6-arbiter-combs \
  --hdlcpp /path/to/hdlcpp --cpphdl /path/to/cpphdl \
  --verilator /path/to/verilator --cxx clang++ --opt-level O2 \
  --cva6-metadata /path/to/retained/ordinary --mode optimize-combs \
  --iterations 1000000 --trials 5 --cpu 2
```

`--ordinary` adds unoptimized C++ execution of the same converted model.
Omit `--mode` to compare both comb schedulers. Native aggregate storage is now
the C++20+ runtime default, with no layout macro or special include order.
`--native-packed` remains a no-op compatibility argument. Rebuild every model
and caller translation unit after updating the runtime.

The complete arbiter exposed two distinct issues:

1. **Hidden helper dependencies (cpphdl correctness).** The baseline comb-only
   optimizer left `__hdlcpp_expr_*` calls opaque; only L1 expanded them before
   rewriting comb reads. On fresh production-metadata conversion the grants
   output failed at validation cycle 256: expected `0x001`, got `0x401` (two grants).
   Expanding generated helpers in both schedulers makes producer reads visible
   to scheduling and memoization. Native C++ inlining is too late to repair a
   missing producer dependency. `GeneratedHelperTest.sh` covers both schedulers,
   direct and serialized collection inputs, and `-O0`/`-O2` execution.
2. **Repeated grant-tree evaluation (hdlcpp layout).** Separate generate loops
   computed all left children, then all right children. Descendants could read
   a right parent left over from the preceding call. The structural proof
   correctly refused complete-write caching, so eleven consumers recomputed
   the tree eleven times per cycle. Fuse adjacent pure continuous equations
   under identical generate controls, evaluating both children before advancing
   to the next level. The existing proof then establishes safe memoization;
   it is not bypassed. Procedural bodies, unknown writes, sequential updates,
   and conflicting scoped declarations do not acquire this transformation.

The key difference is the generated evaluation order, not a new scheduler:

```text
before: for each level/node: left = parent & !selection
        for each level/node: right = parent & selection
after:  for each level/node:
            left = parent & !selection
            right = parent & selection
```

`indexed_comb_proof.sv` adds a tiny binary-tree reproducer with input-dependent
children; the old converter fails its compile-time complete-write assertion.
The fixed conversion passes all 256 input patterns in ordinary execution and
both optimizer modes, at `-O0` and `-O2`. Partial/cyclic/dynamic writes remain
negative proof cases. `test_combs.cpp` also tests prefix fusion and mismatched
loop directions.

Use `profile_context_replay.py --arbiter-cycles 100000` with these executables
and their `verilator-vectors.bin` for gated sampling and optional Callgrind
counts (see `../context_replay/README.md`). In the measured 100,000-cycle work
interval, grant evaluator calls fell from 1,100,000 to 100,000; total simulated
instructions fell from 42,653 to 29,920 per cycle. Verilator used 1,555. Native
samples and exclusive Callgrind costs identify packed-array proxies and
pack/unpack propagation as major remaining costs, not just timestamp checks.
Simulated L1 data-read misses were approximately 0.003 per cycle for both
models, so this experiment does not support a cache-capacity explanation.

With fusion plus native aggregate storage, the count falls further to 16,170
instructions per cycle, but scalar packed arrays still use proxy access:
`array<11, logic<103>, true>::operator[]` alone costs 1,888 instructions/cycle;
the 11- and 15-element four-bit index arrays cost another 2,297. There are still
63 `memset` and 40 `memcpy` calls per cycle. These are measured exclusive costs
and call counts, not estimates from C++ source size. The remaining direction is
native-word lowering for scalar packed-array reads/writes and field boundaries
inside cpphdl, rather than another scheduling mode or unsafe unconditional
memoization. Native aggregate storage helps structs; it does not implement
that scalar-array lowering by itself.

Final serial comparison: one CPU (affinity 2), the same Clang `-O2` toolchain,
1,000,000 timed cycles, five alternating-order trials. All variants checked the
same 20,000 Verilator reference cycles outside timing and produced timed checksum
`90ba20973d5e02b7`. Conversion, compilation, construction, and reference checking
are excluded from the work times:

| Variant | Median work time | Relative to Verilator |
| --- | ---: | ---: |
| Original RTL / Verilator | 0.112225 s | 1.00x |
| Comb-only, helper correctness fix only | 3.190716 s | 28.43x |
| L1 control, helper correctness fix only | 3.170729 s | 28.25x |
| Comb-only, helper fix + loop fusion | 2.430776 s | 21.66x |
| Comb-only, helper fix + native packed storage | 2.211083 s | 19.70x |
| Comb-only, helper fix + fusion + native packed storage | 1.464489 s | 13.05x |

The original broken comb-only build is deliberately absent from the timing
table: it failed output validation. Fusion reduces the valid comb-only time
by 23.8%; fusion plus the explicit layout option reduces it by 54.1% (2.18x
faster). Switching schedulers alone is within measurement noise and is not
retained as a proposed fix. These measurements predate making native packed
storage the C++20+ default; the last row is now the default layout.
This still leaves a substantial Verilator gap; it is not performance parity.

After making native packed storage the C++20+ default, a fresh conversion and
five-trial rerun **without** `--native-packed` or any layout compiler definition
measured 1.457788 s for comb optimization and 0.111627 s for Verilator. The same
20,000 reference cycles and 1,000,000-cycle checksum passed. Results are in
`/tmp/cva6-combs-default/arbiter/`. There are no per-mode layout guards, generated
configuration headers, or include-order requirements.

Detailed commands, hashes, individual trials, and gated profiles from this run
are under `/tmp/cva6-combs-investigation`: `final-comparison/`,
`profile-helper-fix/`, `profile-fusion/`, and `profile-combined/`. The earlier
`fusion-native-packed/summary.json` timings overlap a profiling run and are
superseded by the final serial comparison. `profile-fusion-native-packed/`
was rejected by the input-hash guard and `profile-final/` was stopped by the
low-memory guard; neither is a completed profiling result.

These are complete-arbiter results, **not** a prediction of full-CVA6 speed.
The full processor has not been regenerated or revalidated for these changes.

## Request-only control

From the cpphdl repository root, with both tools built:

```sh
python3 hdlcpp/tests/cva6/check_request_tree.py \
  --cva6-source /path/to/cva6 \
  --output build/request-tree \
  --verilator /path/to/verilator --cpu 2
```

Requires Python 3, Verilator, make, and a C++23 GCC/libstdc++ toolchain with
`libstdc++exp`. Omit `--cpu` when CPU affinity is unavailable. Defaults are
11 and 16 inputs, ten million timed evaluations, and five alternating trials.
Use `--iterations 1000 --trials 1` for a correctness-only smoke run.
`--hdlcpp` and `--cpphdl` can select separately built tool versions for comparison.
`--cxx` selects the host compiler for **both** models. `--opt-level O2` or `O3`
sets both models and drivers, overriding Verilator makefile optimization defaults.

The script converts the original CVA6 `cf_math_pkg.sv`, `lzc.sv`, and
`rr_arb_tree.sv` with hdlcpp, then runs cpphdl in both combinational scheduling
modes. `Root.h` only wires the input and observes the actual converted module's
request-tree output with `DataWidth=1`, `ExtPrio=1`, and `LockIn=0` to isolate
the combinational producer from arbitration state. The SystemVerilog wrapper
observes the same internal
signal in the original RTL. Neither wrapper implements a replacement tree.
Conversion override environment variables are cleared, generated method bodies
are not copied or edited, and source hashes are checked for changes.

Verilator produces the exhaustive reference table. Both unoptimized hdlcpp and
cpphdl execution must match it for every input and three previous output values.
Timed runs use identical width-masked pseudorandom inputs and matching checksums.
Outputs include `commands.json`, `rtl-sha256.json`, `inputs-sha256.json`,
`environment.json`, `results.json`, `summary.json`, generated
sources, and individual build/run logs. The output directory is owned by the
test; reruns replace its generated files.

This is an isolated combinational-block test, not a complete arbiter protocol
test or evidence that the converted CVA6 processor executes software correctly.
