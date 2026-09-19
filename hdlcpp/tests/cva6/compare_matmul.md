# Equivalent-work CVA6 matmul comparison

Build both testbenches with `CVA6_BENCH_STATS` support, then run:

```sh
python3 hdlcpp/tests/cva6/compare_matmul.py \
  --verilator /path/to/cva6/work-ver/Variane_testharness \
  --cpphdl /path/to/fresh/model/run_cpphdl_testharness_opt \
  --output build/matmul-comparison --cpu 2
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
