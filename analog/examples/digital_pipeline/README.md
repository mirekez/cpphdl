# Native class hierarchy and simultaneous digital assignments

These logical models inherit Vdd and Vss but leave both unused. No power
consumption or voltage dependence is implied by their bit operations.

`Pipeline` contains an `Adder8` and two `Register8` C++ IC objects. Each derives from `IC`, declares member ports, and implements `_assign()`. Each class
states its purpose through its typed value signal:

```cpp
SUM = sim_add(A, B); // Compute the assigned 8-bit result.
Q = sim_store(D, CLK); // Store on rising edges; initial value is zero.
```

Here `=` binds the computation or storage that assigns the bit value. Unlike
physical wires, bit signals have one writer. Input 250+10 wraps modulo 256 to 4.
The 1 us clock starts low; its first rising edge is at 0.5 us.

| Time | First Q | Second Q |
|---|---|---|
| Initial | 0 | 0 |
| 0.5 us, after commit | 4 | 0 |
| 1.5 us, after commit | 4 | 4 |

The second register samples the old first-register value. Both next values are
computed before their simultaneous commit, following the useful work/strobe
separation in C++HDL's native examples. Final-value requirements pass. This
example does not model transistor timing, setup/hold, power or metastability.
A digital-to-electrical conversion requires an explicit driver purpose.

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
cmake --build build --target analog_digital_pipeline -j 4
./build/analog_digital_pipeline examples/digital_pipeline/results
```

[main.cpp](main.cpp) contains the full commented C++ class/experiment source.
Inspect `equations.txt`, `trace.csv`,
and `report.json`. Voltages are in volts and times in seconds;
bit columns contain integers or unknown. All results describe the selected models
at recorded sample times. Fixed-step integration does not prove continuous-time
bounds or all-environment behavior.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.
