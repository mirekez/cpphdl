# Define a regulator from basic circuit schemas

The [regulator](../../basic/regulator.hpp) inherits Vdd/Vss and declares REF and
OUT ports, an internal CONTROL wire, and an [opamp](../../basic/opamp.hpp) child
named `error`. Its PMOS pass device supplies output current:

```cpp
OUT = pmos(CONTROL, Vdd, pass_model); // Deliver supply current to OUT.
```

The child amplifier's PLUS port connects to OUT, MINUS to REF, and OUT to CONTROL.
It shares the regulator's supply rails. If OUT exceeds REF, the amplifier raises
the pass gate, reducing conduction. Falling OUT produces the opposite correction.
The complete connections are visible in [regulator.hpp](../../basic/regulator.hpp).

The amplifier is itself a circuit: an NMOS differential pair, a PMOS current mirror,
three explicit resistors and an output capacitor. The regulator expands recursively
to five MOS devices, three resistors and one capacitor. No regulator or amplifier
primitive or special solver state exists. Their currents and stored charge interact
with the surrounding experiment through the same physical equation system.

In [main.cpp](main.cpp), the reference is an explicit ideal 1.2 V source. The output
connects to an external 100 nF capacitance and 100 Ohm load. REF is an electrical
port, so a different reference-generating circuit could replace that stimulus.

| Part | Selected model/configuration |
|---|---|
| Pass transistor | PMOS, beta = 20 mA/V², threshold magnitude = 0.35 V |
| Error amplifier | Basic four-MOS differential amplifier; defaults in [basic/README.md](../../basic/README.md) |
| Input supply | 3.3 V initially, stepping to 2.5 V at 250 us |
| Reference | Ideal 1.2 V source relative to Vss |
| Output storage | 100 nF, initially 0 V |
| Amplifier output storage | 10 nF, initially 0 V at CONTROL |
| Load | 100 Ohm from OUT to Vss |
| Integration | Backward Euler, 500 ns steps, 500 us duration |

After the supply step, the final output is approximately **1.200118 V** and
CONTROL approximately **1.054501 V**. The final [1.18 V, 1.24 V] requirement passes.
The remaining error comes from the input current difference required to supply
the amplifier's output shunt. No fixed gain parameter or ideal assignment sets OUT.

At steady state, when the pass PMOS is saturated, the load current must satisfy:

```text
0.5 * beta_pass * (VIN - CONTROL - threshold)^2 = OUT / Rload
```

The amplifier's input-pair bias and output KCL must also hold; they are documented
in [basic/README.md](../../basic/README.md). The semantic checks verify the pass
relation, small but nonzero reference error, overload failure, and complete supply
KCL including the amplifier's bias and output-shunt currents. The amplifier's bias
is substantial with these educational parameters: around 40 mA at 1.2 V common mode.

Startup overshoots to about **1.319 V at 3 us** because CONTROL starts at 0 V,
enabling the pass device before feedback settles. The requirement is final-only;
it does not assert that startup remains in the final band. There is no soft start
or overvoltage protection. A 1 Ohm load defeats regulation by exceeding the pass
device's available current.

The circuit has limited common-mode range, mirror compliance and pass headroom.
MOS body effects, intrinsic gate charge, channel-length modulation, thermal damage,
protection circuitry and physical reference generation are omitted. Amplifier and
pass-device supply currents are both modeled through their terminal connections.
The supply-impedance test uses 50 ns steps for the coupled supply/bias startup;
500 ns fails Newton convergence in that test. See the numerical limits in
[basic/README.md](../../basic/README.md). This is a prototype topology, not a
characterized physical LDO.

## Build and run

From the `analog/` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target analog_regulator -j 4
./build/analog_regulator examples/regulator/results
```

[main.cpp](main.cpp) contains the experiment;
[regulator.hpp](../../basic/regulator.hpp) and [opamp.hpp](../../basic/opamp.hpp)
contain the component definitions. Inspect `equations.txt`,
`trace.csv`, and `report.json`. Traces expose
VIN, REF, OUT, CONTROL and TAIL in volts, with time in seconds. Device origins and
IC identities show the regulator, amplifier, differential pair and current mirror.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.
Selected-model sampled results do not establish continuous-time bounds or
all-environment regulation and stability.
