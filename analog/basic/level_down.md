# High-voltage decisions to core logic

[`level_down`](level_down.hpp) is an electrical `IC` with inherited `Vdd`/`Vss`,
input `A` and noninverting output `Y`. `Vdd` is the **output/core** supply,
typically 1.2 V. The input may swing to 3.3 V.

A weak PMOS with its gate at ground supplies the internal `inverted` node.
The input controls a high-voltage NMOS that can pull that node toward ground.
A two-transistor core inverter restores the original polarity. Only the HV
NMOS gate touches the high-voltage input; the three other gates remain in the
core domain. Gate, feedback, node and output capacitances remain connected.

This is ratioed logic. A high input must overpower the weak pull-up, drawing
static current. Input charge loads the driving decision latch. Shared supply
impedance, output load and device strength affect the achieved result.
An earlier complementary HV input pair suffered excessive internal kick in
PSP; adding storage alone then missed the delivery deadline. The ratioed
topology addresses both effects, with 80 fF of explicit internal storage.

The generic contract checks 250 MHz alternating input, all nine combinations
of ±10% input/output supplies, 20 and 200 fF loads and a 10 kOhm output load.
Each output must reach below 10% or above 85% of its nominal core supply
within 1 ns and retain that state. Core-device exposure stays below 1.65 V;
node bounds, finite input charge and finite supply loading are checked too.
A 5 pF overload must break the rail contract. A halved timestep must preserve
plateaus within 5 mV. Independently passing solvers are then compared within
5 mV on plateaus and 10 ps on output crossings.

The process experiment in `1G/IHPSG13G2/tests/level_down.py` preserves the
exported circuit and host capacitors, replacing its four channel laws with
explicitly sized PSP devices. Unsupported laws, changed inventories and
inconsistent branch terminals fail binding. The selected input NMOS is HV;
the remaining devices are LV. Sizes are engineering choices, not inferred
from generic beta values. All widths use valid PDK fingers. The process sweep
uses five corners, three temperatures, five supply pairs and two loads.

```sh
cmake --build analog/build --target analog_1g_level_down
ctest --test-dir analog/build --output-on-failure \
    -R 'analog.1g.(level_down\.|ihp_level_down\.pvt)'
```

Both generic flows and their comparison pass all twenty cases. Maximum
cross-solver departure is 0.779 mV on plateaus and 2.324 ps on output crossings.
All 150 process cases also pass. Their largest low output is 0.929 mV,
smallest high is 1.058 V and maximum core-device exposure is 1.353 V.
The report is `analog/build/1G/IHPSG13G2/tests/results/level_down_pvt/checks.json`.
Generic and process results are recorded separately. These experiments
do not qualify layout, mismatch, power-off sequencing or oxide lifetime.
The voltage-domain distinction follows the IHP PDK's
[LV and HV device definitions](https://ihp-open-pdk-docs.readthedocs.io/en/latest/contents/device_libraries/mosfets.html).
This circuit is an internal ADC interface, not a replacement for a pad's ESD
and secondary protection network.
