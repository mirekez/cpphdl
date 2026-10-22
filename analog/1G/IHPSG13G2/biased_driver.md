# MOS-biased and trimmed PAM5 line drivers

[biased_driver.hpp](biased_driver.hpp) adds three reusable IC classes and two
compositions. Each declares inherited supplies, member electrical ports and
line-by-line physical devices in `_assign()`.

`resistor_mirror_bias` connects a resistor from Vdd to a diode-connected NMOS.
`biased_pam5_driver` mirrors that bias into four NMOS tail sinks and steers each
sink between P/N with two NMOS gates. Two 50-ohm pullups, output capacitances and
the external differential termination all influence the achieved voltages.
There is no ideal tail-current source or imposed bias voltage in either model.

`trimmed_mirror_bias` replaces the fixed bias resistor with an 1800-ohm base
branch plus six parallel branches. Branch b has resistance `256000 / 2^b` ohms
and a PMOS supply switch. A low `TRIM_N[b]` gate voltage enables the branch.
`trimmed_pam5_driver` composes this bias with the same steering output stage.
A trim setting changes real conductance; it does not overwrite the solved current.

The generic square-law model uses an illustrative reference-channel strength
chosen for 5 mA per tail at 3.3 V. Its generated Verilog-A retains the same
circuit equations and is compared to native C++ with the common electrical
checker. These parameters are not fitted PSP transistor models.

[tests/driver.py](tests/driver.py) separately simulates the actual IHP
`sg13_hv_nmos`/`sg13_hv_pmos` PSP compact models. Reference and steering MOS devices
use W=100 um, L=0.45 um; tail MOS devices use W=300 um, L=0.45 um; trim PMOS devices
use W=100 um, L=0.45 um. The initial differential load is 100 ohms. Gate stimulus
and supplies are explicit bench sources. These ports still need physical logic
buffers and level shifting in a chip.

The fixed 1480-ohm circuit passes the TT contract. Without trim it fails the
same amplitude requirement at SS/FF. The switched-resistor circuit passes all
three process corners with externally selected settings:

| Process corner | Enabled-branch code | Settled maximum error | Error at 2 ns after gate transition |
| --- | ---: | ---: | ---: |
| TT | 30 | 0.82 mV | 12.78 mV |
| SS | 42 | 2.94 mV | 14.45 mV |
| FF | 20 | 1.68 mV | 14.08 mV |

The common contract requires all five levels, <=6 mV settled error relative to
0.25 V/symbol and 2.8 V output common mode, >=0.2 V tail headroom, <=0.5 mA
settled supply-current error including measured reference current, and <=15 mV
signal error at 2 ns. Supply current is checked after 0.65 UI to exclude bias and
output-capacitor displacement current; the signal still has the explicit 2 ns
check. These are prototype requirements, not Clause 40 transmit-template limits.

Codes are supplied by the test configuration. There is no automatic on-chip
calibration loop yet. These cases use 3.3 V at the simulator's default temperature
and sweep process corners only. Voltage/temperature/mismatch sweeps, the actual
resistor compact model and layout, pad loading, package/magnetics, transmitter
pulse shaping/template qualification and extracted performance remain necessary.
No driver GDS or transistor LVS is claimed by these schematic tests.
