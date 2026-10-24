# C++ inverter to connected IHP layout

[`inverter_layout.py`](inverter_layout.py) implements the two-device
[`cmos<channel_model>` IC](../../basic/inverter.hpp). It reads the actual
[`cmos_channel.va` export](../../basic/generated/cmos_channel.va), requires its
pull-up and pull-down contributions and rejects changed terminals, additional
laws or changed device parameters.

The implementation explicitly selects LV NMOS W/L = 2/0.13 µm and LV PMOS
W/L = 4/0.13 µm, each with one finger. These are engineering geometries;
the generic model's beta and threshold do not uniquely determine process
dimensions. Each device uses the [connected PCell generator](mos-layout.md).
Metal2 joins gates and drains, and Metal2/Metal3 joins sources and substrate or
well contacts to their supply. Every connection is physical geometry.

The resulting bounding area is **143.714 µm²**, including individual guard
rings and local routing. The floorplan follows generated device extents with
a 2 µm gap. A same-net metal bridge closes a short ground-branch notch without
enlarging that boundary. This is a small routed gate, not a complete PHY area
estimate.

[`tests/physical_inverter.cpp`](tests/physical_inverter.cpp) instantiates the
same C++ IC with a finite 10 Ω supply, 50 Ω input source and 160 fF output
load. Both generic solvers independently check inversion, delivered inputs,
settling, output bounds and shared-rail droop before waveform comparison.
Refining from 5 ps to 1 ps reduced the maximum solver difference from 5.734 mV
to 1.136 mV, meeting the unchanged 5 mV bound.

The strict PSP binder uses the same topology and selected dimensions in 45
cases: TT/SS/FF/SF/FS, −40/27/125 °C, and 1.08/1.2/1.32 V. It scales source,
node and capacitor initial voltages together and initializes intrinsic PSP
charge consistently. Every process case must pass the same independent
electrical checks. These process equations differ from the generic square-law
model; their transient waveforms have separate qualification.

The layout regression requires strict four-port transistor LVS with explicit
tap devices and complete local geometry, antenna and pin checks. Global density
failures in this unfilled macro remain recorded, with `full_drc_pass = false`
and `full_chip_drc = false`. Chip acceptance requires zero violations after
integration and fill. PSP schematic checks use ideal body contact bias;
extracted contact/wire resistance, coupling, ESD and full-chip performance
remain to be qualified.

```sh
cmake --build analog/build --target analog_1g_ihp_physical_inverter
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_inverter[.]physical$'
```

The test keeps the intended schematic, GDS, complete DRC/LVS databases,
electrical trajectories and source/tool/PDK/artifact hashes under
`build/1G/IHPSG13G2/tests/results/physical_inverter/`.
