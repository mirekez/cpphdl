# Analog regression tests

This directory is the regression entry point for the C++HDL analog framework.
Tests use explicit failures rather than `assert`, so Release builds keep all checks.
CTest requires CMake and a C++20 compiler. Tests build against the parent package's
native runtime headers without Python, LLVM, Verilator or synthesis.

From the package root:

```bash
cmake -S analog -B analog/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build analog/build -j 4
ctest --test-dir analog/build --output-on-failure -L analog
```

Or from `analog/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target check-analog -j 4
```

`check-analog` builds all required test and example executables, then runs CTest
with label `analog`. Use a separate build directory and `-DCMAKE_CXX_COMPILER=clang++`
when switching compilers. To rerun already built tests:

```bash
ctest --test-dir analog/build --output-on-failure -L analog
```

The root package also enables `CPPHDL_BUILD_ANALOG` by default. After configuring
the ordinary cpphdl package build, run:

```bash
cmake --build build --target check-analog -j 4
ctest --test-dir build --output-on-failure -L analog
```

The standalone analog build is the smallest regression setup. The root package
configuration still requires its normal Clang/LLVM toolchain even when building
only analog targets. No external CPU test repositories are needed for analog tests.

| CTest group | Coverage |
|---|---|
| `analog.semantics` | 41 checks: RC/step refinement, coupled feedback, transistor schemas, loads and shared supply, IC hierarchy/lifetime, active variables, diagnostics, known/unknown events and exact 64-bit arithmetic |
| `analog.cpphdl_interop` | 12 checks: both include orders, native/managed old-state barrier, separate clocks, exact 64-bit transfer, retained ownership, unknown inputs/clocks, duplicate/context/null/width errors and unregistered/unbound outputs |
| `analog.digital_ports` | Digital-only C++17 build with existing lazy ports and native work/strobe state behavior |
| `analog.trace_requirements` | Completed-trajectory assertions: pass/fail/unknown status, conditional guarantees, freeze/empty callbacks and measurement evidence |
| `analog.feedback.*` | LDO dropout, loaded PLL, weak-input AGC, half-step runs and quantitative coarse/fine comparisons using CTest fixtures |
| `analog.core_isolation` | Core-only loaded divider, IC naming and compile-time exclusion of native lazy ports and strobe methods; no native cache clock |
| `analog.mixed_signal` | End-to-end RC sensing, native pipeline latency, loaded output, capacitor continuity and return sensing |
| `analog.examples.*` | Thirteen individual circuit and behavioral experiments, including intentional diagnostics/failure outcomes; the native mixed example is `analog.mixed_signal` |
| `analog.reject_wrong_units` | A capacitor value cannot be passed as resistance |
| `analog.reject_wide_bridge` | A native bridge wider than 64 bits is rejected |
| `analog.reject_static_ports` | Legacy static ports cannot instantiate analog schemas |

[semantics.cpp](semantics.cpp) uses independent physical conservation/equilibrium
checks, including loaded divider ratios, transistor current balance, refinement
toward an RC analytic solution and complete supply KCL.
[cpphdl_interop.cpp](cpphdl_interop.cpp) reverses native registration order and
mixes native registers with managed stores to catch same-edge state propagation.
It also checks that retained native ownership is released after the simulation.
[CompileFail.cmake](CompileFail.cmake) verifies both failure and the expected
compiler diagnostic, so an unrelated include error cannot count as a successful
negative test.

The [CTest example driver](RunExample.cmake) runs each executable and checks both
its exact exit code and the status in its generated report. Artifacts go under
`build/tests/example-results/<name>/` in the standalone build, or
`build/analog/tests/example-results/<name>/` in the package build. The mutual
influence example deliberately fails its output requirement; that expected failure
is a passing regression outcome. An unrelated execution error cannot count as
that expected failure.

There are 31 tests with label `analog`. The optional `run_regressions.py` helper
configures, builds and invokes this same CTest suite; CTest itself does not need it.

Fixed-step numerical limits and selected-model assumptions remain in
[basic/README.md](../basic/README.md) and the
[integration contract](../doc/cpphdl-integration.md).

The feedback checks compare LDO settled voltages within 1 mV; PLL frequency within
12 Hz and mean phase within 0.01 cycles; AGC positive peaks within 2 mV, detector
means within 3 mV and control means within 50 mV. Each nominal/refined run must
also pass its own operating-window requirements. CTest fixtures regenerate both
traces when selecting a convergence test alone:

```bash
ctest --test-dir analog/build --output-on-failure -R 'analog.feedback.*convergence' -j 3
```

The expected-loss tests assert dropout or saturation explicitly. A generic solver
error, missing edges, missing report, or unexpected unsuccessful requirement cannot
count as successful stress behavior.
