# Programmable receive gain

`programmable_receive_gain : IC` uses a cascode amplifier, one output follower
and a low-impedance resistor string. Exactly one of its five `SELECT_N` ports
must be low; the other four must be at the analog supply. These are physical
gate voltages. The parent provides the level translators and selects a setting
during training.

| Low select port | Resistor tap | Intended signal gain |
| --- | ---: | ---: |
| `SELECT_N[0]` | 4 | −0.8 |
| `SELECT_N[1]` | 5 | −1.25 |
| `SELECT_N[2]` | 6 | −2 |
| `SELECT_N[3]` | 7 | −3.5 |
| `SELECT_N[4]` | 8 | −8 |

Eight equal resistors connect OUT to a ninth resistor ending at IN. The
selected tap feeds the amplifier's high-impedance negative input. Its selector
carries gate charging current rather than the resistor string's signal current.
For tap k, Kirchhoff's equations give a gain near `−k / (9 − k)`. This avoids
treating MOS switch resistance as a precise programmable feedback resistor.
The inversion is explicit and must be included in receive polarity handling.

A resistor/capacitor low-pass follows the input's mean for the positive input.
Signal current flows through the string and attached input/output drivers;
it does not flow through a high-impedance bias divider. Finite source impedance
also changes the mean loop's settling time. The fixture uses 50 Ω and waits
16 µs before its symbol checks. Startup and gain changes in a complete receiver
need their own settling interval.

The same select wires connect additional compensation capacitors for lower
gain. Their charge, switch loading and interaction with the amplifier remain
in the shared simulation. Multiple selections or excessive source impedance
can change or defeat the intended gain; the IC does not override the resulting
node voltages.

[`basic/cascode_opamp.hpp`](../../basic/cascode_opamp.hpp) defines the amplifier
from individual NMOS/PMOS devices and stored charge. Supply/reference currents,
body effects in the separate PSP flow, input loading, follower headroom and
capacitor charge all affect the result.

```sh
cmake --build analog/build --target analog_1g_ihp_programmable_receive_gain
ctest --test-dir analog/build --output-on-failure \
    -R '^analog[.]1g[.]ihp_receive_gain[.]' -j 1
```

The tests check the same five signal sequences independently in C++ and
Verilog-A before comparing their waveforms. Separate PSP tests bind every MOS
to reviewed HV widths and lengths, check CSV connectivity against the actual
Verilog-A contributions, retain all declared storage and select valid fingers.
Binding tests reject wrong bodies/gates, swapped dimensions, missing storage
and unknown laws. All five settings pass independently in C++RTL, Verilog-A
and nominal TT/27 °C IHP PSP simulation at a 20 ps maximum signal step.
The generic waveforms also pass the separate pointwise 5 mV comparison bound;
the largest measured difference across observed nodes is 4.451 mV.

| Tap | Measured PSP gain | Maximum aperture residual |
| ---: | ---: | ---: |
| 4 | −0.77066 | 4.055 mV |
| 5 | −1.20220 | 3.929 mV |
| 6 | −1.90952 | 4.386 mV |
| 7 | −3.27694 | 6.072 mV |
| 8 | −7.56743 | 1.961 mV |

These are loaded 125 MBd circuit results, with the original 15 mV aperture
residual bound and 3 pF output load. They establish this nominal fixture;
process corners, dynamic gain changes, the cascaded ADC, physical translators
and layout remain separate work. [The gain controller](../receive_gain_control.md)
provides the digital acquisition policy; it is not yet an integrated analog
AGC or a completed receiver.
