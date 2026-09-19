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
