# Standalone Regression Runners

The Python runners for slices, casts, graph lowering, struct imports and
interface imports use the C++ compiler selected by CMake. Their Verilator
builds also use this compiler, rather than the compiler path recorded when
Verilator itself was built. Timing-enabled builds require C++20.

Native sanitizer tests first build and run an empty program to check the host
runtime. By default they use ASan and UBSan. If that probe reports the known
ASan conflict with an injected `RTLD_DEEPBIND` library, the runner prints a
warning and uses UBSan alone. Other probe errors fail the test. A failing
regression executable is never retried with fewer sanitizers.

Set `CPPHDL_TEST_SANITIZERS=address,undefined` to require both sanitizers,
`CPPHDL_TEST_SANITIZERS=undefined` to select UBSan explicitly, or
`CPPHDL_TEST_SANITIZERS=none` to explicitly disable sanitizer instrumentation.
Run ASan-required checks on a host without the conflicting injected library.

Each run keeps a separate artifact directory under its `--work` path. Compiler,
converter, simulator and sanitizer-probe output is saved there. Use
`ctest --test-dir build --output-on-failure` to display the failing command's
diagnostics and artifact path.

`cpp_graph_memory_scaling` checks cached-comb dependency growth at 80 and 160
stages under a 768 MiB address-space limit, plus incremental AST growth for
128 and 256 template instances. It requires Linux resource limits.

`optimizer_assign_initialization` checks ordinary field initialization mixed
with port bindings and child `_assign()` calls. The caller still executes the
original `_assign()` before `bind_optimized_ports()`: this preserves setup order
and side effects without putting initialization into the per-cycle schedule or
moving it into constructors. Binding analysis recognizes direct writes to known
non-port storage only; unsupported port assignments and setup calls remain
errors. The regression compares ordinary execution with normal and L1 comb
optimization, direct and serialized collection, and O0/O2 host compilation. It
also repeats setup and checks that cycle evaluation never repeats initialization.

`optimizer_specialized_comb` also checks lexical shadowing of module fields and
ports in work, strobe and comb bodies. AST-resolved member references retain
their identity when a nested block or loop declares a same-named local; explicit
`this->` port references still participate in dependency scheduling. Ordinary
and optimized execution must agree for direct and serialized collection, normal
and L1 scheduling, and O0/O2 host compilation.

`optimizer_work_mutation` reads a procedural comb before and after a helper
mutates its input during the same `_work()` call, including a transitive comb
consumer. Complete result initialization does not justify per-clock caching
when an input can change during work. The test preserves ordinary port caching
and checks normal/L1 scheduling, direct/serialized collection and O0/O2 builds.

`optimizer_template_port_initializer` collects complete module types without
instantiating their constructors. A dependent `_PORT(Value)` is not yet a
concrete `function_ref`, but its primary-template initializer must survive
collection and specialization. Two value widths are checked against ordinary
execution with both schedulers, collection paths and host optimization levels.
