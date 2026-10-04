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
