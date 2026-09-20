# Automatic nested packed-store lowering

## Implemented in the normal tool flow

`cpphdl --optimize-combs` and `--optimize-combs-l1` now lower nested indexed
assignments automatically. The existing `hdlcpp` conversion of the original
SystemVerilog is unchanged: the missing optimization was in cpphdl's emission
and runtime primitive, not the RTL conversion's semantics.

No generated design is hand-edited, no register file is replaced, and no new
scheduler, LUT, or CVA6-specific production rule is added.

The change extends the existing `Combs.cpp::lowerPackedBitStores` pass:

- Locate the final subscript in a contiguous subscript chain.
- Evaluate RHS first and retain its reference/temporary lifetime.
- Bind the preceding subscript expression once, before evaluating the final
  index, preserving C++ assignment sequencing and index side effects.
- Dispatch through the existing typed `sv_assign_bit` helper.

For a packed proxy viewing a `logic` element, the helper writes its original
parent store at the proxy's bit offset. It retains element bounds checks and
untouched bits, including physical padding. Wide parents use the existing
single-byte update; native-width parents use masked native-word updates.
Whole-element and unpacked assignments retain their ordinary assignment
semantics through the fallback, rather than being truncated to a bit.

The small primitive is forced inline, like existing scalar assignment helpers,
so parent offsets and bounds can be resolved at its callsite. Without that
annotation, Clang O2 left the new helper out of line and the first automatic
version took 0.719 s. The final version eliminates that remaining boundary.
This is not blanket inlining of generic slice constructors/writeback loops.

## Small end-to-end result

Original `config_pkg.sv` and `ariane_regfile_ff.sv` were converted afresh using
hdlcpp, then optimized by the rebuilt production cpphdl tool. Both modes were
compiled with Clang O2 and tested against captured full-CVA6 matmul inputs.

The resulting L1 executable is **byte-identical to the successful isolated
native-store experiment**, SHA-256:
`838f30e6889d3c667d261b29e7b695c89518afffe9e65eae7a7f598baaa279cc`.
The experiment's 296-byte decoder, with no helper calls, is now produced
automatically rather than by altering its generated source.

Fresh interleaved comparison, five trials per variant, CPU 2, no concurrent
build/profile work from this investigation:

| Version | Median seconds | Timed cycles |
| --- | ---: | ---: |
| Verilator | 0.224442 | 2,313,200 |
| Previous cpphdl L1 | 1.760811 | 2,313,200 |
| Fixed cpphdl full-combs | 0.298771 | 2,313,200 |
| Fixed cpphdl L1 | **0.294118** | 2,313,200 |

L1 takes **83.3% less time / approximately 5.99x throughput**, and remains
approximately **1.31x slower than Verilator** for this replay. All runs produce
checksum `33cd39c93d03a8bc`. Conversion, compilation, input loading, and reference
checking are outside the timing interval. Each timed run is 50 complete replays
of the 46,264-cycle trace, including reset.

The initial fresh-conversion series independently measured 0.301078 s L1,
0.298229 s full-combs, and 0.225703 s Verilator. The paired series above includes
the previous cpphdl binary as an additional contemporaneous control.

## Correctness and regression coverage

- Both optimizer modes, O0 and O2: exhaustive 65,536-input generated-versus-direct
  regression, including nested packed/unpacked arrays, whole-element writes,
  195-bit parent storage, partial writes, and RHS/index side effects.
- Runtime tests cover parent padding, 1/9/21/32/65/129-bit elements, cross-byte
  selections and element bounds. GCC ASan/UBSan and Clang C++17 builds pass.
- Both fresh register-file modes match all 46,264 captured read pairs, all
  8,193 directed records covering 4,096 address/enable combinations, and the
  20,000-output random test plus 200,000-cycle checksum.
- All 23 optimizer/combs regressions and 13 CVA6 Python tests pass.
- The maintained register-file runner now requires the nested-store lowering
  to appear in generated code, preventing an unnoticed return to the slow path.

## Full-core validation scope

Full-core comb code is regenerated with the new optimizer from the retained,
validated SystemVerilog conversion and collection metadata, including parsing
the original final root seed. This is an incremental tool rebuild, **not a new
full SystemVerilog conversion**. The small test above does convert its RTL afresh.

The complete graph is unchanged: 958 instances, 753 scheduled values, 11,176
dynamic evaluators, 12,477 dynamic states and 265 lazy backedges. All generated
interface/state headers are byte-identical to the baseline. Eleven of 30 code
partitions change, with 104 nested assignment sites lowered across the design.

All 30 comb/work/commit partitions are compiled afresh against a frozen copy of
the new runtime; 76 unchanged constructor/harness objects are reused. Their
class layouts and driver ABI do not change. All link inputs and input hashes
are recorded. The old full-core executable remains untouched.

Full matmul passes in 46,264 cycles, and all **21,285 cycle/PC/instruction/
destination/value retirement records** match the previous validated run, which
was checked against the original native Verilator reference.

Five fresh interleaved full-core trials, CPU 2, with no concurrent build/profile
work from this investigation:

| Version | Median work seconds | Range | Total cycles |
| --- | ---: | ---: | ---: |
| Original Verilator executable | 3.474468 | 3.428404–3.524790 | 46,264 |
| Previous cpphdl L1 executable | 34.598274 | 33.899961–35.050503 | 46,264 |
| Fixed cpphdl L1 executable | **32.929996** | 32.277724–33.117267 | 46,264 |

Every run passes matmul. Full-core timers cover 46,254 work cycles, excluding
initialization and 10 reset cycles; both simulators use the same convention.
The native reference remains its original GCC build; both cpphdl builds use
the same Clang O2 cycle-code settings. These compare the existing full run flows,
not a newly rebuilt same-compiler native core.

**Full-core time decreases only 4.82%, below 10%; it remains 9.48x native time.**
The requested automatic block fix achieves the measured 83% benefit, but it does
not solve the whole-CVA6 performance gap. Scheduling, register-copy work and
slice reads were not redesigned, and the isolated block's gain must not be
extrapolated to the whole core.

## Reproduction and artifacts

Production regression:

```sh
cmake --build build --target cpphdl -j2
ctest --test-dir build -R '^optimizer_native_bit_store$' --output-on-failure
python3 hdlcpp/tests/cva6/check_request_tree.py --regfile \
  --cva6-source /home/me/cva6 --output build/regfile-fixed \
  --cpphdl build/cpphdl \
  --verilator /home/me/cva6/tools/verilator-new/bin/verilator \
  --cxx /home/me/scalepnr/.conda/bin/clang++ --opt-level O2 \
  --regfile-trace build/representative-block-20260914/capture-final/matmul-regfile.bin \
  --iterations 2313200 --trials 5 --cpu 2
```

Artifacts: `build/nested-packed-store-20260914/` contains fresh conversion,
generation/build logs, binary hashes, `small-validation.json`,
`small-paired-timings.json`, `small-paired-summary.json`, regression results,
and the full-core incremental rebuild/comparison in `full/`. Local
`check_small.py` and `full_validate.py` record the exact reproduction procedures.

An older collection PCH at
`build/native-stores-20260912/collections/context.pch` was losslessly compressed
to `.pch.gz` to make disk space. No reference executables were removed or changed.
