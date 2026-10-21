# Use device types as C++ variables

`NamedDevices : public IC` exposes member ports and declares its devices
inside `_assign()`:

```cpp
pmos pull_up(A, Vdd);
Y = pull_up;
nmos pull_down(A, Y);
Vss = pull_down;
```

`pmos` and `nmos` are real C++ types. Constructing one registers a physical model;
its purpose declaration joins the exposed terminal to the shared wire. Handles
can leave `_assign()`'s scope while the simulation retains the actual device
relations. A wire copy references the same connection, while device objects are
noncopyable to avoid accidental model duplication.

The physical setup matches the CMOS inverter example: beta=1 mA/V², threshold=0.35 V, supply=1.2 V,
10 pF initially uncharged output, and input rising at 2 us. Final Y is about
11.5 nV and the low-output requirement passes. This demonstrates that compact
purposes and explicit device variables use one physical model substrate.
The channel approximation omits body/leakage/intrinsic charge and physical damage.

IC interfaces are member declarations, following C++HDL. There is no user-defined
IC constructor. `Vdd` and `Vss` are inherited public ports from `IC`, with
separate wires for each instance and explicit supply connections when used.
The testbench binds named ports and calls `elaborate()`; that
guarded lifecycle entry invokes `_assign()` once. Pass ports directly to devices; they carry
the underlying wire or signal identity. Component identity appears in the result artifacts.

## Build and run

From the `analog/` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target analog_transistor_pair -j 4
./build/analog_transistor_pair examples/transistor_pair/results
```

[main.cpp](main.cpp) contains the full commented C++ class/experiment source.
Inspect `equations.txt`, `trace.csv`,
and `report.json`. Voltages are in volts and times in seconds;
bit columns contain integers or unknown. All results describe the selected models
at recorded sample times. Fixed-step integration does not prove continuous-time
bounds or all-environment behavior.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.
