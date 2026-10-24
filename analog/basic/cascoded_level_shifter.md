# Cascode voltage translation and shared bias

[`cascoded_level_shifter`](cascoded_level_shifter.hpp) is a twelve-MOS IC with
Vlogic, BIAS, A, Y and Y_N ports and inherited Vdd/Vss. Low-voltage input devices
pull through high-voltage cascodes into a cross-coupled PMOS latch. Two output
inverters restore complementary output rails. Each declared gate/source and
gate/drain capacitance connects its actual terminals, including the bias node.

[`level_shift_bias`](cascoded_level_shifter.hpp) supplies BIAS from a resistor
divider, a feed resistor, one diode-connected NMOS and a capacitor. Its
unloaded divider fraction is 0.8 of Vlogic, but the replica current and attached
gate charge change that result. The framework lowers these declarations to
KCL, the channel law and charge equations. For example, the bias-node equation
contains the feed current, replica current and every connected capacitive
current; there is no imposed bias voltage.

```cpp
level_shift_bias bias;
cascoded_level_shifter translator;
bias.Vlogic = logic_supply;
bias.Vdd = io_supply;
bias.Vss = ground;
translator.Vlogic = logic_supply;
translator.Vdd = io_supply;
translator.Vss = ground;
translator.BIAS = bias.BIAS;
translator.A = input;
bias.elaborate();
translator.elaborate();
```

Several translators can connect to BIAS. Their switching then influences each
other through the same finite bias network. A larger bank needs its own bias
drive/storage qualification; the single-translator experiment does not
establish an unlimited fanout.

The direct-HV [ten-MOS circuit](level_shifter.hpp) passes its slower process
fixtures but fails the selected 250 MHz process-corner settling contract. A
cascode implementation addresses that low-input-drive limit. The selected
compact cascode model additionally exposes about −0.105 V output kick at one
supply combination, outside the experiment's −0.1 V bound. Both generic
electrical solvers detect it.

The derived [1G clock interface](../1G/clock_level_shifter.hpp) adds an explicit
200 fF capacitor to each output to correct that behavior. The earlier 100 fF
version passed 18 generic supply/load cases and their solver comparison but
undershot to about −0.106 V with valid IHP fingers. The first actual-IHP sweep
passed 150 cases, but used provisional
single-finger widths beyond the model's declared 10 µm limit. That result is
superseded. Process fixtures now fold total widths into valid fingers. The
geometry-corrected 200 fF interface passes all 150 process cases: five process
corners, three temperatures, five supply combinations and two output loads.
The generic 200 fF circuit and solver comparison are being rechecked. The
geometry-corrected direct-topology negative control still rejects its selected
250 MHz settling case.
The replica needs a declared startup interval before clock transfer
is scored. Process PSP tests include intrinsic device physics and separately
check low-voltage gate differences; they are not identical to the selected
generic channel-and-capacitance equations or a lifetime/layout qualification.
