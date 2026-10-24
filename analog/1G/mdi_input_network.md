# MDI input conditioning

`mdi_input_network : IC` connects the shared line at `P/N` to receiver pins
`PLUS/MINUS` through two 2 kOhm resistors. Two 330-ohm resistors return those
pins to a common bias made by a 1 kOhm / 1 kOhm supply divider and 2 pF storage.
All six resistors participate in both directions. Line current changes the bias,
receiver loading changes the line, and finite supply impedance changes both.

The receiver owns a 100-ohm differential load at PLUS/MINUS. The transmitter's
two 50-ohm pullups terminate the MDI. Adding another 100-ohm termination at
P/N would change the circuit and reduce its signal unnecessarily.

With the receiver's 100-ohm load, the effective differential output load is
`100 || (2 * 330) = 86.842 ohms`. The network attenuates the achieved MDI
differential voltage by `86.842 / (4000 + 86.842) = 0.02125`. This formula
uses the actual line voltage, not an unloaded command. The reference divider
is also loaded, so the achieved common mode must be solved rather than
substituting an ideal 1.65 V source.

The standalone fixture uses the two-driver linear Norton equivalent: 25 ohms
per leg, 2.3 V command common mode and up to 2 V differential command. The
network delivers approximately 1.91 V active input common mode and no more
than 45 mV differential input. Four additional zero-difference transitions
between 1.65 and 3.3 V exercise disabled-line common-mode settling. Receiver
operation during those transitions is outside this module's contract.

## Verification

`tests/mdi_input_network.cpp` exports and simulates the actual class. The bench
includes a 1-ohm supply impedance, supply decoupling, the receiver's 100-ohm
load and 330 fF input storage on each leg. It covers all 625 pairs of the
main/history transmitter states and four common-mode transitions at 125 MBd.

The Python checker independently solves six loaded DC node equations. For
common-mode transitions, it independently integrates three capacitor states
with 1 ps RK4, retaining the finite source and supply impedances. Checking a
DC result before this real RC network has settled would impose the wrong
requirement. Every checking-aperture state must be within 0.1 mV of these
references. Both flows also run at 20 and 10 ps; their aperture observations
must agree within 20 microvolts after each passes its own oracle.

```bash
cmake --build analog/build --target analog_1g_mdi_input_network
ctest --test-dir analog/build -R 'analog.1g.mdi_input_network' --output-on-failure
```

`generated/OneGMDIInputNetwork.va` is generated from this same IC. This module
qualifies passive conditioning under the declared load. It does not qualify
the transistor receiver at the resulting common mode, its ADC codes, analog
echo cancellation, ESD capacitance, resistor corners, extracted routing or a
complete PHY. Those connections need their own coupled tests.
