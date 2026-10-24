# Cascode amplifier

`cascode_opamp : IC` declares a differential NMOS input pair, NMOS cascodes,
PMOS cascodes/current mirror and a controlled NMOS tail. The nine MOS devices
share physical wires; no ideal gain or output voltage source enforces the
amplifier's purpose. `PLUS`, `MINUS` and `OUT` are signal ports. `BIAS_N`,
`BIAS_P` and `TAIL_BIAS` are external bias ports in addition to inherited
`Vdd` and `Vss`.

The caller provides the biases and any surrounding feedback. Finite headroom,
output loading, input and internal capacitances, the output shunt and supply
current affect the achievable response. `compensation` sets the output storage;
changing it affects settling and stability. The input/output MOS model parameters
are explicit members in [the class](cascode_opamp.hpp).

[The programmable receive gain stage](../1G/IHPSG13G2/programmable_receive_gain.md)
provides a tested feedback application, bias circuits and an output follower.
Its C++RTL and Verilog-A regressions independently check five gain settings and
compare their waveforms. A separate IHP PSP flow binds that application's MOS
geometry and checks the resulting transistor simulation. This application
result does not qualify arbitrary biases, loads or process implementations.

`generated/cascode_opamp.va` is generated from this class by the root
`generate-veriloga` target. The external bias ports remain external in that
export; selecting this class does not insert a hidden bias primitive.
