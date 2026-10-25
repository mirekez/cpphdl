# An external component can defeat a purpose and influence its neighbor

Two default-declared `cmos<>` IC objects share a rail supplied through 200 Ohm.
Their A, Vdd and Vss member ports are bound explicitly before `elaborate()`.
Both intend a high output for input zero. Their 10 pF output capacitors start
charged to 1.2 V, representing the settled initial condition.

An external driver purpose is attached to the loaded output. Initially it selects
Vdd through 100 Ohm. At 1 us it selects Vss instead. All earlier device purposes
remain. The load now competes with the IC's pull-up in the same coupled solve.

| Quantity | Before switching | Final value |
|---|---|---|
| Loaded output | 1.2 V | **0.031041 V** |
| Shared Vdd | 1.2 V | **1.137918 V** |
| Neighbor output | 1.2 V | **1.137918 V** |

The shunt sinks about 0.310408 mA; its current through the supply impedance causes
about 62.082 mV rail droop. The neighbor's unchanged class responds to that changed
rail. No isolated input/output function or special case forces these consequences.

**Expected executable exit code is 1 and report status is `failed`.** This is a
successful physical simulation demonstrating an unmet purpose. The loaded IC's
>=1 V requirement fails; its neighbor's passes. The sampled supply assumption
stays valid. The class needs a sufficient load envelope or a stronger realization
if high output must be guaranteed in this environment.

This implements requirements #1 and #2: successive device purposes coexist and
influence connected devices throughout simulated time. Permanent physical damage
is not modeled; thermal/failure laws would be needed for that claim.

IC interfaces are member declarations, following C++HDL. There is no user-defined
IC constructor. `Vdd` and `Vss` are inherited public ports from `IC`, with
separate wires for each instance and explicit supply connections when used.
The testbench binds named ports and calls `elaborate()`; that
guarded lifecycle entry invokes `_assign()` once. Pass ports directly to devices; they carry
the underlying wire or signal identity. Component identity appears in the result artifacts.

## Build and run

From the `analog/` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON
cmake --build build --target analog_mutual_influence -j 4
./build/analog_mutual_influence examples/mutual_influence/results
```

[main.cpp](main.cpp) contains the full commented C++ class/experiment source.
Inspect `equations.txt`, `trace.csv`,
and `report.json`. Voltages are in volts and times in seconds;
bit columns contain integers or unknown. All results describe the selected models
at recorded sample times. Fixed-step integration does not prove continuous-time
bounds or all-environment behavior.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.
