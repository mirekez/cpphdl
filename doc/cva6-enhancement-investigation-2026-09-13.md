# Why the request-tree improvement did not fix full CVA6

Follow-up: `cva6-matmul-regenerated-2026-09-13.md` records the completed fresh
build with both fixes. Boot-ROM retirement now progresses, but matmul still
times out at the Verilator reference's clock count.

## Findings

The isolated request-tree speedup was real, but it did not establish that the
whole converted CPU was correct. Two independent correctness defects exist on
the converted AXI path. A faster arbiter cannot compensate for them.

### 1. Producer sharing widened dependencies incorrectly

`Combs.cpp` previously ran `redirectProjectedPortBindings()`. It recognized
converter-style names and replaced a projected field producer with a read of
the complete aggregate followed by field selection. It checked naming and comb
bindings, **not dependency equivalence or absence of newly introduced cycles**.

This transformation is present in the fresh CVA6 output:

- The generated `axi_mux.h` binds `mst_req_o_out__field_ar_valid` to
  `mst_req_o_ar_valid_comb_func()`. For the multi-input mux this field needs the
  AR spill register's valid output.
- The optimized inner crossbar's evaluator 3050 instead reads
  `p3835_storage.ar_valid` for ROM port 1.
- Evaluator 3835 builds the whole ROM mux request. Before reaching `ar_valid`,
  it evaluates AW, W, and B-ready producers as well. Those dependencies were
  not required by the original field getter.

The new `ProjectedHandshakeRoot` regression isolates the unsafe transformation
without CVA6 or array conversions. Its field graph is simply
`input -> valid -> ready`. The whole-bus getter writes ready before valid.
Redirecting the valid getter to that bus introduces a false cycle and exposes a
partially written value. The saved old compiler fails **both scheduler modes**
on the first cycle:

```text
cycle=0 valid=0/1 ready=0/1
```

The optimizer reports one lazy-cycle back-edge for this originally acyclic
field graph. Thus this is a demonstrated compiler correctness bug, not merely
a speculation about cache overhead. It also explains why the old tests missed
it: they checked sharing on independent arithmetic fields, without handshake
feedback through another field.

**Change:** remove the unsafe redirection and its name/path-recovery machinery.
Keep the field-level producers supplied by hdlcpp and share actual common
subexpressions through the existing graph. No new scheduler or hardware-specific
replacement is added. The retired metadata slot remains readable in v4
collection files.

On this regression the fixed L1 graph has three scheduled values, **zero dynamic
evaluators and zero lazy-cycle back-edges**, instead of three dynamic evaluators
and one false back-edge. Preserving the right dependencies also removes cache
machinery here; no extra scheduling layer is needed.

### 2. Packed/unpacked field copies broadcast the wrong value

The generated inner crossbar produces
`array<10, logic<1>, true>` for AR-valid. Its interface wrapper stores the field
as `array<10, logic<1>>` and uses `sv_assign_field` to copy it.

The helper previously recognized matching layouts, but treated this different
layout as a scalar broadcast: every destination element received the complete
source vector, narrowed to one bit. With only ROM bit 1 set, every destination
element became zero. Conversely, setting bit 0 asserted every destination.

A standalone runtime test reproduces this without cpphdl optimization:

```text
packed/unpacked copy mask=1 port=1 actual=1 expected=0
```

The helper from `c1ed6f8^` also reproduces ROM bit 1 disappearing, using current
unchanged array types. This defect predates the latest request-tree
specialization change.

**Change:** copy matching array shapes element-by-element when only packing
differs, including register current-value bases. Retain existing scalar and
same-layout assignment behavior. Regression coverage includes all 1,024 ten-bit
request vectors, both copy directions, register sources, and 1/9/65-bit elements.

## Full-model diagnostic and limits

Artifacts are under `build/array-boundary-20260913/`.

A deliberately partial diagnostic recompiles only dynamic partition 5 with a
generic packed-to-unpacked copy overload. It reuses the original model's other
objects and original header/PCH contents through an explicit baseline overlay.
This is an ablation, **not a fully rebuilt fixed model or a performance result**.
The original generated hardware and baseline executable remain unchanged.

- Copy-only probe: still times out after 5,000 total clocks, with zero observed
  RVFI commits.
- Instrumented copy-only probe: at the optimized copy boundary the ROM source
  is already zero, including at system clock 1,000. Fixing that copy alone
  cannot restore a value lost upstream.
- The earlier lazy-getter diagnostic reports the inner ROM request as one but
  the wrapper request as zero. Those getters recompute state; this observation
  is not an inert trace of the optimized schedule. The standalone regressions
  avoid relying on that observer effect.

These experiments establish two real defects and show that the array copy is
not the sole blocker. They do **not** establish that these are the only full-CPU
defects, or that removing the redirection alone makes CVA6 pass matmul.

The maintained compiler and runtime fixes require regeneration/recompilation of
the full model before an end-to-end success or speedup can be claimed. The last
validly recorded full comparison remains: Verilator passes at 69,315 total
clocks; cpphdl times out at that same limit. No new equivalent-output speed ratio
is claimed in this investigation.

## Validation

- Targeted optimizer tests pass: native bit stores, specialized combs, projected
  combs, and the new projected handshake. The handshake test checks both scheduler
  modes at `-O0` and `-O2` against the unoptimized field getters.
- Expanded array-copy regression passes at `-O0`, `-O2`, and with ASan/UBSan.
- Full optimizer suite: **15/16 pass**. The remaining
  `optimizer_structural_nttp` failure is the previously recorded aggregate-leak
  check, not a new regression in these changes.
- A v4 handshake collection written by the saved old compiler loads, generates,
  compiles, and runs correctly under the fixed compiler.
- The adjacent array-layout regression passes; shell syntax and `git diff
  --check` pass.
