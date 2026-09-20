# Request-tree benchmark repeat — 2026-09-14

Repeated the earlier small request-tree benchmark through the actual
SystemVerilog → hdlcpp → cpphdl flow, using current tools and runtime headers.
No handwritten replacement or generated-code edits were used.

## Same-setup repeat

Median seconds for **10 million combinational evaluations**, five alternating
trials, CPU 2:

| Inputs | Verilator | cpphdl full scheduler | cpphdl L1 scheduler | L1 / Verilator |
| ---: | ---: | ---: | ---: | ---: |
| 11 | 0.221068 | 0.400419 | 0.415102 | 1.88× |
| 16 | 0.237324 | 0.316055 | 0.316074 | 1.33× |

The previously highlighted 16-input block therefore remains near the earlier
1.32× ratio, rather than the 9.76× gap measured for the complete CVA6 matmul.
These are different workloads: this test measures combinational evaluations,
not complete CPU cycles or program execution.

## Compiler-flag cross-check

Build-log inspection found an important qualification to the earlier setup:
the Verilator benchmark **driver** gets a trailing `-Os` from make defaults,
overriding the requested `-O3`. The combined generated model translation unit
already ends with `-O3`. Cpphdl is compiled with `-O3`.

A second freshly generated series overrides make's optimization variables:
`MAKEFLAGS='OPT_FAST=-O3 OPT_SLOW=-O3 OPT_GLOBAL=-O3'`. The driver and generated
model compile at `-O3`, as verified in their build logs.

| Inputs | Verilator | cpphdl full scheduler | cpphdl L1 scheduler | L1 / Verilator |
| ---: | ---: | ---: | ---: | ---: |
| 11 | 0.219890 | 0.395659 | 0.399806 | 1.82× |
| 16 | 0.235714 | 0.335343 | 0.336636 | 1.43× |

The host is shared and background workloads remain active. Short-run timing
variation is visible: 16-input cpphdl L1 spans 0.312799–0.341999 seconds in the
first series and 0.316035–0.341184 seconds in the second. The cpphdl timing
difference between series should not be attributed to the Verilator flag
change. No performance optimization was introduced in this rerun.

## Correctness and configuration

- Original CVA6 `cf_math_pkg.sv`, `lzc.sv`, and `rr_arb_tree.sv`; RTL hashes
  remain unchanged.
- Both schedulers and plain hdlcpp execution match Verilator's exhaustive
  reference for every request value and three preceding output values.
- This covers 6,144 cases per 11-input check and 196,608 per 16-input check.
- All timed checksums match: `091fca4c88a85d71` for 11 inputs and
  `2cfac66cb2cae871` for 16 inputs.
- GCC 15.2.0; Verilator 5.049-devel; `DataWidth=1`, `ExtPrio=1`, `LockIn=0`.
- Source/tool/runtime hash checks pass. No production source changes are made.

## Reproduction

From the cpphdl checkout:

```sh
python3 hdlcpp/tests/cva6/check_request_tree.py \
  --cva6-source /home/me/cva6 \
  --output build/request-tree-comparison-20260914 \
  --verilator /home/me/cva6/tools/verilator-new/bin/verilator \
  --cxx g++ --cpu 2 --inputs 11 16 --iterations 10000000 --trials 5
```

For the flag-controlled series, prepend the `MAKEFLAGS` environment setting
above and use the output subdirectory `matched-o3`.

Both series retain commands, generated sources, build logs, exhaustive reference
tables, and raw trials. Summary:
`build/request-tree-comparison-20260914/summary.json`.
