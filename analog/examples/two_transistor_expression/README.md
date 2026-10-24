# The requested purpose declarations and active variable types

This is the primary example for requirement #2. `WireOutput : public IC` declares
A and Y as `_PORT(electrical)` members and inherits Vdd and Vss from `IC`. Its `_assign()` contains:

```cpp
auto pmos_vdd = pmos::from(Vdd); // Source-bound PMOS alias.
Y = pmos_vdd(A); // First device enables the output.
Vss = nmos(A, Y); // Second disables it by pulling low; both work together.
```

These are purpose declarations that also handle the required connection or
assignment. The first device is enabled by low A, the second by high A. Their
shared output is continuously determined by both physical relations and its load.
The second declaration retains the first device; it does not overwrite a stored
voltage on Y. There is no composition operator or custom grammar.

`ActiveOutput : public IC` changes the output port to `_PORT(capacitance) Y{10_pF, Vss}` and uses the
same two purpose lines. Its type now adds charge storage. The wire-only version
receives the equivalent explicit `Y = capacitor(Vss, 10_pF)` purpose externally.
Their output traces match at equal initial conditions, as a semantic test verifies.

The source is ordinary C++20; ICs are ordinary classes. The public library also
supports typed `pmos`, `nmos`, and `cmos<>` variables. Examples 08 and 09 show
individual device variables and selectable IC model types.

Supply=1.2 V; initial output=0 V; input rises at 2 us. Both outputs settle near
11.5 nV at 4 us, satisfying their low-output requirements. This endpoint is evidence
for the selected models and experiment, not a universal inversion or timing proof.
The simple channel model has no body effects, leakage, intrinsic charge or damage.

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
cmake --build build --target analog_two_transistor_expression -j 4
./build/analog_two_transistor_expression examples/two_transistor_expression/results
```

[main.cpp](main.cpp) contains the full commented C++ class/experiment source.
Inspect `equations.txt`, `trace.csv`,
and `report.json`. Voltages are in volts and times in seconds;
bit columns contain integers or unknown. All results describe the selected models
at recorded sample times. Fixed-step integration does not prove continuous-time
bounds or all-environment behavior.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.
