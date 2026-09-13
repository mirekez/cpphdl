# CVA6 request tree through the actual two-tool pipeline

## What changed

The retained production change is **36 net lines in `Combs.cpp`, including
explanatory comments**. There is no hand-written tree, LUT, model-name match,
new scheduler, or optimization switch.

The missing optimization was at the cpphdl input boundary. Hdlcpp already
emits the generated branches as `if constexpr`, but cpphdl's AST collector
does not normally visit implicit template instantiations. It therefore missed
already-instantiated getter bodies. Reconciliation could also replace a
specialized body with its generic source pattern, restoring discarded branches
and hiding a simple assignment from the existing scheduler.

The fix:

1. Collect already-instantiated, zero-argument reference-returning getters of
   reached concrete module classes. Do not instantiate additional methods or
   traverse all standard-library specializations.
2. Retain compact concrete bodies instead of overwriting them with generic
   patterns. Track any surviving template parameters from the retained body.
   Expanded bodies and structural NTTP classes keep the bounded existing path.
3. Omit discarded branches and peel declaration-free, single-statement scopes.
   Preserve declaration scopes and destructor lifetimes.

This exposes the input producer as an ordinary assignment. The existing alias,
dependency, and native-bit-store lowering then operate normally. The isolated
tree changes from one dynamic evaluator to two scheduled values with **zero
dynamic evaluators and zero dynamic states**, in both scheduling modes.
The existing native-store lowering is unchanged; its earlier performance gain
is already included in this report's control, not counted again.

No hdlcpp production change was necessary for this defect: its existing output
contains the required specialization information. The regression explicitly
runs hdlcpp first, rather than supplying a manually extracted C++ method.
Fresh collection is required because old serialized collections already lost
these facts. The existing collection runner invalidates collections when the
cpphdl executable is newer; do not force `CPPHDL_REUSE_COMB_COLLECTIONS=1` when
updating the collector. No new flow option is required.

## Automatic correctness test

```sh
python3 hdlcpp/tests/cva6/check_request_tree.py \
  --cva6-source /home/me/cva6 \
  --output build/request-tree \
  --verilator /home/me/cva6/tools/verilator-new/bin/verilator --cpu 2
ctest --test-dir build -R '^optimizer_specialized_comb$' --output-on-failure
```

The first command converts untouched CVA6 `cf_math_pkg.sv`, `lzc.sv`, and
`rr_arb_tree.sv`, runs cpphdl in both modes, builds emitted C++, and compares
against Verilator built from the same RTL. Conversion overrides are cleared;
source hashes are recorded and verified unchanged. Wrappers only wire and
observe the actual DUT. No generated method is copied, patched, or replaced.
The isolated configuration uses `DataWidth=1`, `ExtPrio=1`, and `LockIn=0`;
this is not a test of the arbiter's sequential protocol.

- Both plain hdlcpp and optimized cpphdl agree with Verilator for all 11- and
  16-bit inputs and three previous output patterns: 6,144 and 196,608 cases
  respectively, per scheduling mode. Additional 2-, 3-, and 8-input exhaustive
  smoke tests pass.
- The optimizer regression covers both branch choices, surviving width
  parameters, and a destructor whose scope affects the subsequent result.
  It passes both scheduler modes at `-O0` and `-O2`, over 65,536 inputs.
  The saved pre-change compiler fails the generated-branch check; an initial
  narrower specialization-preservation form also fails the dependent-width
  case. The retained implementation handles both.
- The focused L1 regression also passes AddressSanitizer and UBSan.
- The final optimizer suite passes **14 of 15** tests. The remaining
  `optimizer_structural_nttp` failure reproduces with the saved pre-change
  compiler; this change does not address it.

## Measurement protocol

Artifacts are in `build/automatic-flow-20260912/`. `control/` uses the saved
pre-change compiler; `specialized/` uses the rebuilt compiler. Both start with
the same original RTL through the maintained script. The final comparison
alternates five trials per variant, ten million width-masked pseudorandom
inputs, CPU 2 affinity, identical checksums, GCC 15.2.0 `-O3`, and the local
Verilator 5.049 build. Timing excludes conversion, compilation, and exhaustive
validation. The earlier 0.301-second extracted-kernel result is not the control
for this new, complete-pipeline experiment.

`collect.sh` recollects all 11 full-model shards and regenerates the existing
L1 flow. `build_model.py` preserves the current production binary, rebuilds
the generated evaluators and their PCH, and relinks the native harness. Module
headers, runtime code, constructor objects, and their ABI are unchanged. The
full model uses its established Clang 21.1.3 `-O2` flags; the Verilator reference
is the existing `/home/me/cva6/work-ver/Variane_testharness` example.
`compare.py` alternates five 5,000-cycle trials and two trials each at 0, 1,000,
and 10,000 cycles, followed by the block comparison. No compilation runs during
these final measurements. The machine's unrelated background process is not
stopped, so these are local measurements rather than universal guarantees.

The unchanged matrix ELF SHA-256 is
`a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c`.
The full-core conversion retains its existing support maps; the no-override,
untouched-RTL claim above applies specifically to the new isolated pipeline
test, not to removal of all pre-existing CVA6 conversion workarounds.

## Rejected experiment

Function-local readiness/epoch mirrors of existing shared cache timestamps
did not provide a useful improvement. The 16-input block measured about
0.454 seconds per ten million evaluations, versus an initial control around
0.444 seconds. That implementation was removed completely; no dormant flag or
alternate evaluator path remains. Its artifacts are in `paired/`.

## Results

Median block time, seconds per ten million evaluations:

| Inputs | Scheduler | Before | Automatic pipeline | Reduction | Verilator |
| ---: | --- | ---: | ---: | ---: | ---: |
| 11 | full | 0.51544 | 0.39447 | 23.5% | 0.21928 |
| 11 | L1 | 0.51846 | 0.39691 | 23.4% | 0.21928 |
| 16 | full | 0.44343 | 0.30848 | 30.4% | 0.23327 |
| 16 | L1 | 0.44467 | **0.30716** | **30.9%** | **0.23327** |

Thus the requested approximately 0.301-second result is reproduced through
actual **SV -> hdlcpp -> cpphdl -> host compiler -> execution**, without a
hand-written replacement or extracted kernel. The 16-input L1 result is
**1.32x Verilator's time**, not equal to Verilator. The 11-input result is about
1.81x. All timed checksums agree: `091fca4c88a85d71` for 11 inputs and
`2cfac66cb2cae871` for 16 inputs. Raw trials are in `micro.json`.

Full-core median process wall time, same ELF and 5,000-cycle cap:

| Variant | Seconds |
| --- | ---: |
| Saved production binary | 3.64757 |
| Regenerated automatic pipeline | 3.63900 |
| Existing Verilator example | 0.68361 |

**There is no meaningful whole-core improvement: only 0.24% elapsed reduction.**
The requested greater-than-10% gate is met for the isolated block, not the
whole CVA6 run. This change is retained for making the requested block
optimization automatic, not presented as another whole-core speedup.

The 1,000-to-10,000-cycle slopes are approximately 737, 720, and 57.3
microseconds per cycle respectively. Even that startup-adjusted comparison
improves by only 2.4%, below the threshold. The candidate remains about 5.32x
slower than Verilator at the short cap and 12.57x by advancing-model slope.
The full graph still has 9,914 dynamic evaluators and 11,416 dynamic states;
preserving specialization facts is not a general proof of complete writes or
a replacement for the remaining dependency/effect work.

Finally, the converted core still times out after 5,000 cycles with **zero
retired instructions**, unchanged from control. These full-core timing rows
are not evidence of architectural equivalence or successful program execution.
The Verilator completion rerun prints `PASSED` and succeeds after **69,315
cycles**, in about 4.73 seconds (`verilator-completion.log`).
Block-level equivalence is established independently by the exhaustive RTL
test. Full-model measurements and logs are in `comparison.json`, `summary.json`,
and `commits.log`; the rebuilt candidate is installed as the native harness's
`run_cpphdl_testharness_opt`.
