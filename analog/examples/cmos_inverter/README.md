# Two C++ purposes form an inverter

Read `Inverter : public IC` in `main.cpp`: it declares A and Y member ports,
and inherits Vdd and Vss from `IC`.
Its `_assign()` declares:

```cpp
auto pmos_vdd = pmos::from(Vdd);
Y = pmos_vdd(A); // Enable the output from Vdd at low A.
Vss = nmos(A, Y); // Correct/disable it by pulling low at high A.
```

These are purpose declarations. The second line adding a device does not erase
the first device: both remain connected to Y. The framework lowers them to a
PMOS drain at Y/source at Vdd and an NMOS drain at Y/source at Vss. The alias
is an ordinary C++ callable with the source bound. There is no hidden compiler
rule that assumes every CMOS circuit is an inverter.

The two channel models share beta=1 mA/V² and threshold magnitude=0.35 V.
Supply is 1.2 V, external charge storage is 10 pF initially at 0 V. Input starts
at 0 V, then changes to 1.2 V at 2 us. Output rises toward 1.2 V and then falls,
ending around 11.5 nV at 4 us. That tiny endpoint reflects the numerical
residual criterion; leakage is omitted. The <=1 mV output requirement passes.

Read the generated KCL: both channel currents and capacitor current participate
at one node. The model omits intrinsic charge, leakage, body effects and process
corners. It is an educational channel model, not a fabricated transistor specification.

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
cmake --build build --target analog_cmos_inverter -j 4
./build/analog_cmos_inverter examples/cmos_inverter/results
```

[main.cpp](main.cpp) contains the full commented C++ class/experiment source.
Inspect `equations.txt`, `trace.csv`,
and `report.json`. Voltages are in volts and times in seconds;
bit columns contain integers or unknown. All results describe the selected models
at recorded sample times. Fixed-step integration does not prove continuous-time
bounds or all-environment behavior.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.
