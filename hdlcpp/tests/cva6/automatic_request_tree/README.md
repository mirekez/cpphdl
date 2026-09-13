# Automatic request-tree regression

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
`--cxx` selects the host compiler for the cpphdl model; Verilator uses its
configured host compiler.

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
Outputs include `commands.json`, `rtl-sha256.json`, `results.json`, generated
sources, and individual build/run logs. The output directory is owned by the
test; reruns replace its generated files.

This is an isolated combinational-block test, not a complete arbiter protocol
test or evidence that the converted CVA6 processor executes software correctly.
