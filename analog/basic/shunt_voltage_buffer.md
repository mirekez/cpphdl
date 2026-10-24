# A shunt voltage buffer

`shunt_voltage_buffer : IC` regulates an output node that receives current
from connected circuits. A cascode OTA compares `OUT` with `IN`. When `OUT`
rises, the OTA raises the NMOS sink's gate voltage, increasing discharge.
A real pullup resistor supplies the minimum load. Output storage, OTA
compensation and gate coupling remain in the shared equations.

`BIAS_N`, `BIAS_P` and `TAIL_BIAS` are explicit bias ports; `Vdd` and `Vss`
come from `IC`. The class contains the OTA, sink, resistors and capacitors.
It does not introduce a regulator primitive. External current can exceed its
sink range or disturb loop stability, so regulation is a tested purpose,
not an unconditional assignment of `OUT` to `IN`.

The generic eight-lane test in `1G/IHPSG13G2/tests/receive_common_reference.*`
checks this topology with finite line/supply impedances and an exact RC
response oracle. The current nominal IHP configuration fails regulation and
is experimental. It is not selected by the qualified receiver. Its generated
Verilog-A reproduces the declared generic circuit; export is not physical
qualification. See [the current boundary](../1G/IHPSG13G2/qualification-status.md).
