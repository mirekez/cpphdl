# Low dropout regulator

[main.cpp](main.cpp) builds a 1.2 V supply from the reusable
[`ldo` class](../../basic/ldo.hpp). `REF` and `OUT` are member ports;
`Vdd` and `Vss` come from `IC`. The source, output capacitor and loads are
external devices and participate in the same equation system.

The circuit adds a PMOS pass channel to supply OUT, a 10 kOhm / 10 kOhm divider
to measure it, then the transistor [op-amp](../../basic/opamp.hpp) to correct
the pass gate. An excessive feedback voltage raises CONTROL and reduces PMOS
current. The pass model has beta = 0.2 A/V² and threshold magnitude 0.35 V.
The external 1 uF capacitor stores output charge. These are educational model
parameters, not ratings of a fabricated IC.

```text
VIN ---- PMOS ---------------- OUT ---- 200 Ohm ---- Vss
           gate = CONTROL       |
                                +---- 200 Ohm ---- switched NMOS ---- Vss
                                +---- 1 uF ------- Vss
                                +---- 10 kOhm ---- FEEDBACK ---- 10 kOhm ---- Vss
error amplifier: PLUS = FEEDBACK, MINUS = REF, OUT = CONTROL
```

The intended output is `REF * (1 + upper / lower)`, or 1.2 V for REF = 0.6 V.
The engine does not impose this equation as an ideal voltage source. It assembles
terminal currents, including the amplifier bias and divider load. In particular:

```text
Cout * dVout/dt = Ipass - Iload - Idivider
(Vout - Vfeedback) / Rupper = Vfeedback / Rlower
```

The pass and amplifier currents follow the framework MOS equations. Finite gain,
available gate swing, load and supply headroom determine whether the purpose holds.
See TI's [linear regulator fundamentals](https://www.ti.com/video/series/power-fundamentals-linear-regulator-fundamentals.html)
for the pass-device/error-amplifier/feedback organization.

From the package root:

```bash
cmake -S analog -B analog/build -DCMAKE_BUILD_TYPE=Release
cmake --build analog/build --target analog_ldo
./analog/build/analog_ldo analog/out/ldo nominal
./analog/build/analog_ldo analog/out/ldo-dropout dropout
./analog/build/analog_ldo analog/out/ldo-refined refined
ctest --test-dir analog/build --output-on-failure -R 'analog\.(examples\.ldo|feedback\.ldo)'
```

The optional arguments are output directory, then scenario. Generated files are
`trace.csv`, `equations.txt` and `report.json`. The trace columns are time in
seconds, VIN, OUT and CONTROL in volts. Report obligations include checks over
settling windows, rather than only the last sample.

| Scenario | Stimulus and checks |
|---|---|
| `nominal` | VIN falls from 1.6 to 1.4 V at 1 ms; a second 200 Ohm load turns on at 2 ms. Check 1.18–1.22 V before the steps and after each recovery. |
| `dropout` | VIN instead falls to 1.15 V. Verify the output falls below the 1.2 V target and the pass gate approaches its lower swing limit. This is an expected loss of regulation. |
| `refined` | Repeat nominal with 0.5 us rather than 1 us steps. |

At 1 us steps, nominal settled output is approximately 1.20055 V initially,
1.20044 V after the line step and 1.20036 V after the load step. In dropout,
the late output is about 1.070 V and CONTROL about 0.099 V. A passing dropout
report means these stress expectations passed; it does not mean regulation held.

This simple LDO has no current limiting, protection, bandgap reference or validated
stability specification. Low dropout refers to the demonstrated supply headroom;
it does not promise regulation at arbitrarily small headroom. The amplifier also
consumes bias current, so this is not a low quiescent current design.

The [independent Verilog-A/ngspice comparison](../../VerilogA/tests/README.md)
resolves startup with 2 ns / 1 ns native steps. It found that the coarse steps
used by this longer example damp startup ringing substantially even when late
regulation is correct. Use the finer experiment for waveform accuracy; the
millisecond example checks settled recovery windows.
