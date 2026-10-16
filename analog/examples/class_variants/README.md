# Choose IC realization with native C++ types

The example declares two IC variables:

```cpp
cmos<fast_model> fast;
cmos<channel_model> channel;

fast.A = A;
fast.Vdd = Vdd;
fast.Vss = Vss;
fast.elaborate();

channel.A = A;
channel.Vdd = Vdd;
channel.Vss = Vss;
channel.elaborate();
```

Both derive from `IC` and have the same A, Y, Vdd and Vss member ports and inversion purpose. The faster-model policy
elaborates a [level_inverter](../../basic/level_inverter.hpp) child IC. Its two
controlled-resistor purposes provide complementary paths to the actual rails.
The channel policy contains
the PMOS enable and NMOS disable purpose declarations. All models remain connected
to external loads and supplies. A C++ template choice changes model detail without
introducing a new text language or hiding inverter topology in a compiler rule.

Each output carries 10 pF initially at zero; supply is 1.2 V. Input rises at
200 ns, with a 400 ns stop and 1 ns maximum step. Final outputs are approximately
69.4 uV (behavioral) and 0.291 uV (channel), satisfying <=1 mV requirements.
The transients differ. The level-inverter defaults are 2 kOhm on resistance,
with a [0.35 V, 0.85 V] interpolation window relative to its own Vss. No storage
is built into that schema; the external capacitors set the experiment's dynamics.

A controlled resistor is a general bidirectional path governed by a separate
control/reference voltage. Its law is `I = G * (Va - Vb)`, with rising or falling
conductance selected by the schema. The level inverter defines how two such paths
connect; the solver has no inverter law. See [basic/README.md](../../basic/README.md)
for the complete equations, parameter contract and port meanings. Generated device
origins point into the schema header, and the report retains the child IC identity.

These are computational/model-detail choices. No wall-clock speed ratio or
validated greater precision is claimed. Behavioral replacement needs a reference,
observations, envelope and error tolerance; C++ type compatibility alone is not
that evidence.

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
cmake --build build --target analog_class_variants -j 4
./build/analog_class_variants examples/class_variants/results
```

[main.cpp](main.cpp) contains the full commented C++ class/experiment source.
Inspect `equations.txt`, `trace.csv`,
and `report.json`. Voltages are in volts and times in seconds;
bit columns contain integers or unknown. All results describe the selected models
at recorded sample times. Fixed-step integration does not prove continuous-time
bounds or all-environment behavior.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.
