# Generated expression helpers

Runtime helper operations must use named methods or ordinary statement blocks,
not local lambda objects. Cpphdl's RTL frontend does not lower capturing lambda
calls. Lambdas used only for unevaluated type selection are a separate concern.

Generate-local conditional expressions retain `if constexpr` in named methods.
Generated loop indices are explicit value parameters, including indices replaced
by constant expressions. Procedural local variables retain reference binding.
Helper methods are collected separately during parsing so creating a helper
cannot invalidate references into the module's method vector.

Runtime-width concatenation is a pure named operation on an explicitly supplied
array of value/width pairs. Braced initialization evaluates those pairs in order.
All signal reads remain at the call site: moving a partial combinational read
into a method that implicitly reads module state can incorrectly invoke its own
producer, creating recursion or reading stale intermediate state. Width zero is
a no-op; widths at least 64 replace the accumulator without a host-width shift.

Packed-array adaptation and aggregate update helpers preserve their original
assignment/packing decisions. They are generated only when needed. Procedural
packed-field updates use an ordinary scoped block, keeping writes visible to
the combinational extraction pass. The field projector recognizes that block
as one read-modify-write operation and drops unrelated sibling fields. Otherwise
it falls back to evaluating the whole aggregate, introducing false ready/valid
dependencies even though the source SystemVerilog has no combinational cycle.

Definite-write proofs are optimizer metadata, not hardware. Generated proof
definitions are available to native C++ and to cpphdl's comb optimization modes
(`CPPHDL_COMB_OPTIMIZATION`), but excluded from ordinary `SYNTHESIS` translation.
This avoids trying to emit the compile-time proof interpreter as RTL without
removing its evidence from the native optimizer.

Native L1 optimization must expand generated `__hdlcpp_expr_*` helpers before
dependency and complete-write analysis. Simply emitting a named C++ call hides
producer reads from cpphdl; the host compiler can inline that call later, but
cannot retroactively share the HDL producer evaluations. The native optimizer
preserves parameter/local scopes, including references and nested helper calls.
Its native-only closure lowering is not fed into RTL conversion. Expansion is
bounded and recursive methods fall back to ordinary calls. Helper signatures
and parameter scopes survive collection save/load and template reconciliation;
v4 collections remain readable, while new collections use v5.

`tests/optimizer/GeneratedHelperTest.sh` checks the native integration at O0/O2,
both directly and from a saved collection loaded through a declaration-only
seed. It covers two template instances, nested helpers, field-shadowing
parameters and reference updates. The full small bus replay remains the
performance and all-output equivalence gate; this integration fix alone is not
a claim of an improvement over the older lambda-based model.

`tests/expression_helpers.sh` checks generated native C++ at O0/O2 against an
independent oracle and optionally original-SystemVerilog Verilator simulation.
It exercises both generate branches, loop-local variables, a partial-write
dependency chain, 64 runtime concatenation widths, and zero/64/65-bit helper
boundaries. Existing packed-array, blocking-write, and indexed-proof tests cover
the neighboring representation and optimizer contracts.

This removes runtime helper closures, not all constexpr/type-selection lambdas.
It is not a claim that cpphdl's RTL round-trip is complete: the current frontend
still mis-emits structural template identifiers, and the small helper fixture
exposes unsupported standard-array parameters and loop/declaration lowering.
Native replay equivalence and RTL round-trip validity are separate gates.
