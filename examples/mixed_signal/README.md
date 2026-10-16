# Mixed analog and digital logic

Open [main.cpp](main.cpp). This entry links to the implementation in
[analog/examples/mixed_signal/main.cpp](../../analog/examples/mixed_signal/main.cpp), so both locations
show the same example.

The circuit connects a loaded RC input to a threshold sensor, a native C++HDL
two-register pipeline, and a finite-resistance analog output driver. Output
capacitance and a load determine the achieved voltage; a second sensor reads it
back into logic. The source contains comments and checks for pipeline latency,
capacitor voltage continuity and the loaded final voltage.

The `sim_` prefix marks ideal threshold interpretation, behavioral output drive
and host co-simulation adapters; these calls do not define physical sensing or
output-stage circuits.

From the package root:

```bash
cmake -S analog -B analog/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build analog/build --target analog_mixed_signal -j 4
./analog/build/analog_mixed_signal analog/build/mixed-signal-results
ctest --test-dir analog/build --output-on-failure -R '^analog.mixed_signal$'
```

The executable writes `equations.txt`, `trace.csv` and `report.json` into the
specified results directory. The [full explanation](../../analog/examples/mixed_signal/README.md)
documents connections, timing, expected values and model limitations.

This example uses the physical simulation engine. Its `analog_mixed_signal` target is
owned by `analog/CMakeLists.txt`; the digital examples' automatic RTL conversion
does not process it.
