# Native aggregate lowering: CVA6 small replay audit

## Result, not a parity claim

The retained change is automatic generated-type layout lowering, not a
handwritten crossbar, arbiter, LUT, or trace-specific replacement. It reduces
work time by **34.0% against the older, faster L1 baseline**, but the small
replay remains **28.7 times slower than Verilator**. This does not demonstrate
competitive full-CVA6 performance. Full CVA6 was not regenerated or timed.

Final alternating-order, CPU-2-pinned comparison, five trials each:

| Executable | Median work seconds | Trial range |
| --- | ---: | ---: |
| Original Verilator replay | 0.163584409 | 0.161961288–0.165876341 |
| Frozen older L1 | 7.121704265 | 7.092791907–7.214365466 |
| Frozen corrected lambda-free L1 | 8.041861340 | 7.869704142–8.071776502 |
| Retained native-layout production code | 4.701013751 | 4.623227606–4.765337883 |

Every trial checks all **4,052 output bits on all 46,264 cycles** before
timing, and reports `checksum=0261761b2a1c459e`. Parsing, preparation,
validation and compilation are outside the work timer. The timing loop
still updates inputs, executes work, reads outputs and computes its checksum;
it does not skip consecutive equal inputs. Reset settling is unchanged.

Both model builds use Clang 21 and `-O2`; the existing Verilator binary retains
its original conda/toolchain flags. These are not claims of byte-identical
compiler command lines. The final cpphdl model is regenerated with the current
`/home/me/cpphdl/build/cpphdl`, not the older experimental compiler binary.

Evidence is outside the repository:

```
/home/me/cpphdl-validation-20260917/native-final/comparison/
/home/me/cpphdl-validation-20260917/native-final/runtime-commands.json
/home/me/cpphdl-validation-20260917/native-final/current-build.log
```

`comparison/inputs.json` fingerprints the tools, headers, trace, replay context,
generated headers and executables, and the comparator verifies them again
after measurement. Per-trial logs and unrounded times are retained. The
native bit-view subsequently received a logical-width metadata accessor.
After regeneration and recompilation, both object files and the executable
are byte-identical to the measured build, and the full trace passes again.
The original timing manifest is preserved; `current-inputs.json` and
`final-build-equivalence.json` record this final source/build check.
The runtime compilation is serial; the largest measured compiler process uses
1,376,156 KiB RSS. That is compilation memory, not the simulation work timer.

## What is retained

* hdlcpp emits exact-owner layout metadata for generated packed structs:
  field count, logical width, field offsets, and native-field eligibility.
* `convert_packed<T>` copies fields only when the source and target layouts
  prove compatible. Other cases use the original width-preserving pack/unpack
  operation. Unions and metadata inherited by custom derived classes do not
  qualify for the fast path.
* With `CPPHDL_NATIVE_PACKED`, arrays of eligible generated structs store
  actual elements rather than reconstructing a complete packed object on each
  field access. Nested eligible packed arrays work the same way. Untagged
  types keep their existing representation.
* Bit-vector imports extract exact-width elements with bounded byte loads.
  All-zero imports initialize one logical zero element and replicate it.
  The zero test examines **every byte**, not `bool(logic)`, which only examines
  the low word. A low-word-only prototype failed the trace and was discarded.

The independent measured stages justified the retained combination: native
storage/import/zero handling improved the older baseline by about 17%; keeping
compatible struct values structural then improved that intermediate model by
more than 10%. Direct projected-field helper generation was removed after its
incremental improvement failed the 10% threshold. No new scheduler is retained.

Use `--native-packed` with `hdlcpp/tests/cva6/check_context_replay.py`. For a
custom flow, pass `-DCPPHDL_NATIVE_PACKED` both after cpphdl's `--` and to every
model/driver C++ compilation. Rebuild all translation units: this is an explicit
ABI choice. See `hdlcpp/tests/cva6/context_replay/README.md` for its storage/API
contract. Generated source contains no new expression-helper lambdas from this
change; the optimizer's existing internal wrappers are a separate mechanism.

## Experiments and rollback decisions

All variants were isolated under `/home/me/cpphdl-validation-20260917/`.
Failed ideas were never merged into production. Numbers from different
experiment rounds are diagnostic, not one interleaved ranking; the final
comparison above is the retained-code result.

| Variant or group | Observation | Decision |
| --- | --- | --- |
| Broad packed-to-unpacked/addressable-array rewrites (`native-layout`, `native-all`, `native-runtime`) | Exposed raw-stride and packed-proxy ABI problems; corrected probes did not beat L1 | Excluded |
| Model unity build with host optimization (`unity-native`) | 7.659 s, did not beat the older baseline | Excluded |
| Specialized semantic scheduling (`semantic-schedule`) | 7.709 s | Excluded |
| Eager scheduling of the original generated grant tree (`semantic-eager`) | Output mismatch at cycle 273 | Rejected before timing |
| Native packed storage alone (`native-packed`, `native-regions`) | 16.548/15.978 s; import dominated | Reworked, not retained in that form |
| Bounded-byte native import alone (`native-import`) | Repeated median 6.650 s versus older 7.211 s: below 10% | Not retained alone |
| Native import plus whole-value zero handling (`native-zero`) | Repeated median 5.979 s versus older 7.211 s | Retained as part of native storage |
| Const runtime byte slices (`byte-slices`) | 9.828 s on the original representation | Excluded |
| Proof-gated generate-scope fusion (`fused-schedule`) | Independent tree oracle passes; context timing did not improve by 10% | Not merged as a speed fix |
| Typed child-port inlining/eager schedule (`typed-schedule`) | Corrected implicit-conversion issue, but replay became slower | Excluded |
| Structural conversion alone (`structural-values`) | 7.357/7.502 s; no older-baseline win | Not retained alone |
| Structural conversion plus native storage (`structural-native`) | Five-trial 4.346 s before hardening/ablation | Retained mechanism, hardened and simplified |
| Native scalar lanes (`native-lanes`) | 5.972 s versus native-zero 5.979 s; broad version also broke raw boundary ABI | Excluded |
| Eager/native/whole-program combination (`native-schedule`, `native-whole-program`) | 3.649/1.819 s, but omitted imported context | Not accepted as representative results |
| Context-preserving whole-program/aggressive host flags (`native-context-whole`) | Three-trial 3.668 s on the projected prototype | Diagnostic build configuration, not the retained-code comparison |
| Const byte slices on structural/native storage (`structural-byte`) | 4.118 versus matched 4.433 s: 7.1% | Excluded |
| Specialized/fused scheduler with context (`context-schedule`) | 8.025 s despite passing outputs | Excluded |
| Extra projected-field helpers (`production-native` versus `native-minimal`) | Matched medians 4.401 versus 4.775 s: 7.8% | Removed |
| Extra structural array conversion (`structural-arrays`) | 4.321 versus projected 4.401 s: 1.8%; both extras together still below 10% versus minimal | Removed |
| Cached constant-field port aliases (`native-port-alias`) | 306 fewer dynamic states, 4.812 s; no convincing benefit | Excluded |

The separate word/netlist backend retry got past C++ to SV generation but not
valid specialized helper lowering. Earlier original-SV CXXRTL timings are
**not** measurements of the required C++ conversion flow and are not used here.

## Why the remaining Verilator gap is real

The final replay imports the original demand classification: **8,692 matched
nodes, 8,334 demand entries**. The resulting model still has **4,303 dynamic
evaluators and 5,390 dynamic states**, plus 268 scheduled values. Removing that
context produces a different compiler experiment; passing this input trace
alone does not prove it models the surrounding CVA6 scheduling workload.

Inspection of the local Verilator sources confirms two relevant differences:

* `tools/verilator-new/src/V3DfgDfgToAst.cpp` shares producers and eliminates
  redundant variable representations before emitting expressions.
* `tools/verilator-new/src/V3Sched.cpp` schedules trigger/phase regions and
  handles combinational cycles at that level. The measured generated model
  uses native scalar/word operations inside those regions, not a retained
  accessor/cache boundary for every generated C++ field.

A timer-only PC sample of the minimal native prototype puts 57.1% of samples
inside generated evaluators, 5.9% in generated work chunks, 7.8% in runtime
slices and 5.3% in named pack helpers. Evaluator samples include their arithmetic
and inlined helpers; **they do not prove timestamp checks alone cost 57%**.
Indeed, deleting hundreds of forwarding cache states did not produce a win.

The supported conclusion is that native aggregate representation removes a
substantial conversion tax, but is not a substitute for a value-level,
phase-correct compiler representation. The experiments do not establish a
safe, representative replacement of that remaining evaluator graph. In
particular, neither the context-free 1.819 s result nor a LUT rewrite is a
validated route to full-CVA6 parity.

## Correctness and tests

* The new `native_packed_layout` regression runs both storage modes at `-O0`
  and `-O2`, and both modes under ASan/UBSan: all pass. It covers every one-hot
  bit of a 408-bit nested array, 2,048 patterns, overlapping slice assignment,
  wide fields, zero imports, integer-containing structs, field-layout
  mismatches, custom packers and inherited metadata.
* `test_combs`, `test_structs`, the packed-struct-array, blocking-array-update,
  expression-helper, indexed-write-proof and constant-width regressions pass.
* C++17 `ConstexprHelpers`, `PackedWriteback` (404,588 checks), `ArrayUnpacked`
  (`--noveril`) and `PointerPorts` pass. The default, flag-free runtime remains
  C++17-compatible; native aggregate storage requires C++20 or newer.
* The replay/measurement Python suite passes 14 tests.
* The larger module text-expectation suite is **not green**. An external
  diagnostic collector compared every expectation with the frozen pre-change
  hdlcpp: after updating expectations affected by this change, there are no
  newly failing expectations, but 32 distinct existing text mismatches remain.
  Production test assertions were not disabled. This is not reported as a
  passing full module suite.
* `git diff --check` passes. Build artifacts, profiles and generated models
  remain outside `/home/me/cpphdl/`.
