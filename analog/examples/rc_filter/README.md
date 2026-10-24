# RC filter with an active variable

`LowPass : public IC` declares IN and OUT as `_PORT(...)` members and inherits
Vss. The inherited Vdd is unused and contributes no equation. Its `_PORT(capacitance) OUT{1_nF, Vss}` member supplies a shared node with storage. `OUT = resistor(IN, 1_kOhm)` adds the purpose that delivers the
input through a finite resistance. Storage corrects fast changes and loads IN.

R=1 kOhm, C=1 nF, initial output=0 V, input=1 V. The framework generates
`(Vout-Vin)/R + C*dVout/dt = 0`; the input source current is solved as well.
At 5 us with 50 ns backward-Euler steps, `1-(1/1.05)^100` predicts
**0.992396 V**. The continuous answer is `1-exp(-5)` = 0.993262 V. Their
difference is integration error; the semantic tests halve the step and confirm
convergence toward the analytic response. The final output requirement passes.

The active type does not force OUT to an independently calculated voltage.
Another device attached to OUT changes the same KCL equation. The selected
components have no leakage, parasitics or manufacturing variation.

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
cmake --build build --target analog_rc_filter -j 4
./build/analog_rc_filter examples/rc_filter/results
```

[main.cpp](main.cpp) contains the full commented C++ class/experiment source.
Inspect `equations.txt`, `trace.csv`,
and `report.json`. Voltages are in volts and times in seconds;
bit columns contain integers or unknown. All results describe the selected models
at recorded sample times. Fixed-step integration does not prove continuous-time
bounds or all-environment behavior.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.
