# CVA6 conversion and simulation performance experiments

Experiments: 2026-09-11; final validation: 2026-09-12.
Acceptance criterion: at least **10% lower median elapsed time**
on the measured workload. Each additional change is compared with the retained
configuration, rather than claiming the combined gain for a small addition.

Environment: x86-64 KVM guest, eight visible CPUs, 7.2 GiB RAM; reported CPU model
AMD Ryzen 5 3500. Runs were not CPU-pinned and ordinary background services were
not stopped. Final comparisons use contemporary trials, not yesterday's absolute
screening times.

## Changes

- `include/cpphdl_array.h`: decode unpacked-array elements with byte copies.
  These elements already use `sizeof(TYPE) * 8` storage strides. Keep the existing
  element conversion, which handles logical widths and padding.
- `hdlcpp/hdlcpp_common.cc` and `hdlcpp/hdlcpp_frontend.cc`: return cached metadata
  maps by const reference and borrow them at frequently used width/argument/port
  lookups, avoiding repeated map allocations and copies.

## Experiments

Positive percentages mean less elapsed time. Runtime screening used three runs
of the native converted CVA6 harness with the same ELF and 5,000-cycle limit.
Conversion screening used decoder, CSR register file, and instruction-cache
emission with existing metadata. The final comparison uses nine modules.

| Experiment | Screening result | Decision |
| --- | ---: | --- |
| Link optimized objects before constructor objects | 0.9% | Rejected |
| Masked-byte writes for all slice sizes | -9.8% | Reverted |
| Native-word writes for narrow slices | -9.0% | Reverted |
| Byte-copy unpacked-array decoding | 14.5% | Retained: 16.3% after full rebuild |
| Direct array packing, added to faster unpacking | -8.6% | Rejected |
| Borrow cached metadata maps | 28.8% | Retained: 23.7% on nine modules |
| TCMalloc, added to borrowed metadata maps | 9.2% | Rejected |
| jemalloc, added to borrowed metadata maps | 9.3% | Rejected |
| Cache parsed configuration-name sets | 0.2% | Reverted |

Allocator substitutions initially improved the original converter by about 22%,
but did not meet the threshold after fixing metadata copying. Neither allocator
is enabled or added as a dependency.

Runtime screening substituted selected out-of-line template instantiations using
the existing objects. These trials identify candidates; the final runtime result
must come from a normal rebuild of the complete harness. No template-override
object is added to the production build.

## Final measurements

### Simulation

The production harness was rebuilt normally, including constructors and runtime
partitions; all three precompiled headers were refreshed. Five trials per binary
used the same ELF and 5,000-cycle limit. Baseline/retained order alternated, with
Verilator also run in each round. No compilation or conversion benchmark ran
concurrently with these trials. Times include process startup.

| Simulator | Median seconds | Cycles/second |
| --- | ---: | ---: |
| Saved CppHDL baseline | 9.3940 | 532.3 |
| Rebuilt CppHDL with byte-copy unpacking | 7.8668 | 635.6 |
| Existing Verilator example | 0.7578 | 6,598.0 |

CppHDL uses **16.3% less elapsed time**, equivalent to **19.4% more cycles/second**.
It still takes **10.38 times** the Verilator elapsed time on this limited workload.
All ten CppHDL runs have identical output hashes and the same timeout exit status.
This is not a claim of equivalent architectural execution or application completion.

Raw elapsed seconds in trial order:

- Baseline: 9.5415, 9.1737, 9.1999, 9.3940, 9.3941.
- Retained: 7.9069, 7.8801, 7.8274, 7.8668, 7.8284.
- Verilator: 0.9353, 0.7488, 0.7592, 0.7441, 0.7578.

### Conversion

Three complete trials per converter used alternating baseline/retained order,
without concurrent compilation or runtime benchmarks. Every conversion succeeded;
all 54 generated header hashes match their corresponding baseline source header.

| Converter | Trial totals, seconds | Median seconds |
| --- | --- | ---: |
| Saved baseline | 88.3945, 88.9867, 88.7765 | 88.7765 |
| Borrowed metadata maps | 67.7332, 68.3930, 66.7281 | 67.7332 |

The retained converter uses **23.7% less elapsed time**, or **1.31x throughput**,
on the nine-module suite. This is the acceptance workload for the single metadata
optimization; it does not improve every module by 10% individually.

| Module | Baseline median, seconds | Retained median, seconds | Elapsed reduction |
| --- | ---: | ---: | ---: |
| decoder | 22.1932 | 18.6842 | 15.8% |
| csr_regfile | 44.2322 | 27.4043 | 38.0% |
| cva6_icache | 0.8922 | 0.8410 | 5.7% |
| cva6 | 13.1279 | 13.2345 | -0.8% |
| frontend | 1.6478 | 1.4416 | 12.5% |
| load_store_unit | 5.5541 | 5.3993 | 2.8% |
| ariane_regfile_ff | 0.1309 | 0.1228 | 6.2% |
| alu | 0.5706 | 0.3995 | 30.0% |
| branch_unit | 0.2943 | 0.2825 | 4.0% |

Suite medians are medians of complete trial totals, not sums of module medians.

The nine conversion sources are:

1. `core/decoder.sv`
2. `core/csr_regfile.sv`
3. `core/cache_subsystem/cva6_icache.sv`
4. `core/cva6.sv`
5. `core/frontend/frontend.sv`
6. `core/load_store_unit.sv`
7. `core/ariane_regfile_ff.sv`
8. `core/alu.sv`
9. `core/branch_unit.sv`

Conversion timings measure emission using established metadata, including each
converter process's startup. They do not measure an entire clean RTL conversion,
metadata convergence, C++ compilation, or linking. Runs are serialized against
the shared scratch directory. Two exploratory overlapping conversion trials were
discarded and replaced by serialized comparisons.

## Validation and limitations

- Seven relevant datatype tests pass, including new coverage of byte strides,
  padding, short inputs, and elements wider than 64 bits.
- Assertion-enabled combinational and struct converter tests pass.
- Assertion-enabled module tests fail at the same existing `type_width<addr_t>()`
  expectation with both the saved baseline converter and the retained converter.
  A release-mode zero exit code is not treated as a passing module suite because
  that build disables assertions.
- The simulation retains its existing timeout and zero-commit behavior, also
  confirmed with commit tracing enabled on the baseline and rebuilt CppHDL
  binaries. These are simulator throughput results, not successful matrix-program
  completion times. A separate Verilator run with the same ELF and a 100,000-cycle
  ceiling passes after 69,315 cycles (reported wall time 4.897 seconds, one run).
  CppHDL therefore does not yet demonstrate equivalent application execution.
- The original runtime bit-slice implementation is restored exactly. Existing
  unrelated working-tree changes are preserved.

## Reproduction artifacts

Local logs, timing JSON, baseline binaries, and benchmark drivers are under
`build/performance-20260911/`. The runtime workload is:

```sh
cd hdlcpp/tests/cva6/cpphdl_testharness
./run_cpphdl_testharness_opt ../matrix_multiply.riscv 5000
```

ELF SHA-256:
`a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c`.
Runtime compiler: Clang 21.1.3, `-O2`, using the existing CVA6 build configuration
(constructor partitions remain `-O0 -fno-inline`).
Converter compiler: the existing GCC release configuration (`-O3`).

The final repeated comparisons can be rerun from the repository root:

```sh
python3 build/performance-20260911/final_comparison.py runtime
python3 build/performance-20260911/final_comparison.py conversion
```

The runtime driver uses `work-ver/Variane_testharness --max-cycles=5000` for
Verilator. Timing records are `verified-runtime.json` and
`verified-conversion.json`; per-module conversion records and logs use the
`verified-conversion-*` prefix. Baseline binaries are preserved locally.
