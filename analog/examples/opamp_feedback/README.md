# Correct a transistor amplifier with feedback

The reusable [non_inverting_amplifier](../../basic/non_inverting_amplifier.hpp)
contains an [opamp](../../basic/opamp.hpp) and a
[resistor_divider](../../basic/resistor_divider.hpp). Its IN and OUT ports and
inherited Vdd/Vss ports connect through [main.cpp](main.cpp).

The op-amp contains two NMOS input devices, a PMOS current mirror, a tail resistor,
a mirror bias-return resistor, an output shunt resistor and an output capacitor.
Its definition and full parameter list are in [basic/README.md](../../basic/README.md).
There is no amplifier primitive, fixed gain parameter, or hidden controller state.

The divider TOP connects to OUT, BOTTOM to Vss, and TAP to the amplifier MINUS
port. IN connects to PLUS. An increase at IN increases output charging current;
the divider returns part of OUT to MINUS, opposing that increase. All devices
remain in the same physical solve. The divider also loads the output.

The input is 0.5 V and the supply is 3.3 V. The feedback resistors are 10 kOhm
each; the external load is 100 kOhm. The amplifier's explicit 100 kOhm output
shunt also demands current. Output storage is 10 nF initially at 0 V. MOS devices
use beta = 200 mA/V² and threshold magnitude = 0.35 V; the shared tail resistor
is 10 Ohm and the mirror bias return is 1 GOhm.

At DC, `FB = OUT / 2`, but the amplifier needs input error to deliver current.
In the saturated operating region, the independent conservation equations are:

```text
Ileft  = beta / 2 * (IN - TAIL - threshold)^2
Iright = beta / 2 * (FB - TAIL - threshold)^2
TAIL / Rtail = Ileft + Iright
Imirror = Ileft - (Vdd - MIRROR) / Rbias_return
Imirror - Iright = OUT * (1 / 20k + 1 / 100k + 1 / 100k)
```

The 20 kOhm term is the entire feedback divider; the other terms are the external
load and explicit output shunt. Solving these relations predicts **OUT approximately
0.994192 V**, with FB approximately 0.497096 V. The 40 us simulation at 100 ns
steps reaches this equilibrium and satisfies the final [0.99 V, 1.01 V] requirement.
The semantic test independently solves the pair bias and loaded output KCL.

The lighter feedback divider and load suit this amplifier's finite drive. For
comparison, the former 1 kOhm divider resistors and 10 kOhm load give about
0.948239 V with the same transistor model. Changing to an even heavier load also
reduces achieved gain, as a separate semantic check verifies. Neither the intended
gain nor the component's C++ type forces a numerical result.

This single-stage amplifier has limited common-mode range and voltage swing, and
its bias current depends on common mode. Supply current is present through the
PMOS mirror and NMOS pair. Intrinsic MOS charge, body effects, channel-length
modulation, noise and thermal limits are omitted. The explicit output capacitor
supplies the selected dynamics. This is an educational topology, not a characterized
precision op-amp. See [basic/README.md](../../basic/README.md) for model boundaries.

## Build and run

From the `analog/` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON
cmake --build build --target analog_opamp_feedback -j 4
./build/analog_opamp_feedback examples/opamp_feedback/results
```

[main.cpp](main.cpp) contains the experiment. Inspect
`equations.txt`, `trace.csv`, and
`report.json`. The trace exposes OUT, FB, TAIL and MIRROR in
volts with time in seconds. Equation origins point into the basic schema headers,
showing which child IC declared each MOS device, resistor and capacitor.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions. Passing sampled requirements does not prove
continuous-time bounds or correctness for every load or supply.
