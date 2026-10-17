# Generated basic IC modules

Every basic IC class has a module here, using its default C++ parameters and
explicit inherited Vdd/Vss ports. All ports are bidirectional electrical nodes.
Nested classes are flattened into their transistor, resistor and capacitor
contributions; IC names and original declaration locations remain in comments.
The C++ classes remain the definitions to edit.

| Verilog-A module | C++ definition | Purpose |
|---|---|---|
| [cmos_channel.va](cmos_channel.va) | `cmos<channel_model>` in [inverter.hpp](../inverter.hpp) | Complementary MOS inverter |
| [cmos_fast.va](cmos_fast.va) | `cmos<fast_model>` in [inverter.hpp](../inverter.hpp) | Explicit simulation approximation using controlled conductances |
| [level_inverter.va](level_inverter.va) | [level_inverter.hpp](../level_inverter.hpp) | Explicit simulation approximation using controlled conductances |
| [nmos_differential_pair.va](nmos_differential_pair.va) | [nmos_differential_pair.hpp](../nmos_differential_pair.hpp) | Differential current steering |
| [pmos_current_mirror.va](pmos_current_mirror.va) | [pmos_current_mirror.hpp](../pmos_current_mirror.hpp) | Matched sourcing branches |
| [opamp.va](opamp.va) | [opamp.hpp](../opamp.hpp) | Transistor amplifier with explicit compensation |
| [non_inverting_amplifier.va](non_inverting_amplifier.va) | [non_inverting_amplifier.hpp](../non_inverting_amplifier.hpp) | Amplifier with resistive feedback |
| [regulator.va](regulator.va) | [regulator.hpp](../regulator.hpp) | Pass transistor with output/reference feedback |
| [resistor_divider.va](resistor_divider.va) | [resistor_divider.hpp](../resistor_divider.hpp) | Loaded divider |
| [rc_filter.va](rc_filter.va) | [rc_filter.hpp](../rc_filter.hpp) | Series resistance and shunt storage |
| [ldo.va](ldo.va) | [ldo.hpp](../ldo.hpp) | Divider-feedback low-dropout regulator |
| [cmos_nand.va](cmos_nand.va), [cmos_xor.va](cmos_xor.va) | [cmos_logic.hpp](../cmos_logic.hpp) | MOS gate networks |
| [current_starved_inverter.va](current_starved_inverter.va), [ring_vco.va](ring_vco.va) | [ring_vco.hpp](../ring_vco.hpp) | Controlled inverter stage and three-stage oscillator |
| [lead_lag_filter.va](lead_lag_filter.va), [pll.va](pll.va) | [pll.hpp](../pll.hpp) | Passive loop filter and physical phase-locked loop |
| [variable_gain_amplifier.va](variable_gain_amplifier.va), [mos_peak_detector.va](mos_peak_detector.va), [error_integrator.va](error_integrator.va), [agc.va](agc.va) | [agc.hpp](../agc.hpp) | Gain stage, detector, controller and composed AGC |

MOS currents use the framework's square-law channel model, not BSIM. The two
simulation approximations are marked in their headers; they retain their selected
controlled-resistance laws. Parameters are elaborated SI constants, so changing a
C++ parameter requires regenerating the module. Supply voltage and loading remain
host connections and can influence every device.

Capacitor initial voltages are comments in reusable modules, not permanent voltage
constraints. A host analysis must apply the intended initial charges. In particular,
a ring oscillator needs asymmetric startup conditions. The complete
[example bundles](../../examples/generated/README.md) expose internal nodes and
supply explicit SPICE initial conditions.

From the package root, regenerate and verify:

```bash
cmake --build analog/build --target generate-veriloga -j 4
ctest --test-dir analog/build -R 'analog.veriloga.generated' --output-on-failure
```

`analog.veriloga.generated` checks reproducibility against the C++ definitions.
With OpenVAF enabled, `analog.veriloga.generated_compile` compiles every generated
module. See the [backend documentation](../../VerilogA/README.md) for tool setup,
root `cpphdl --verilog-a` conversion, simulation and independent solver comparisons.
