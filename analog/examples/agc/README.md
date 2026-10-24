# Automatic gain control

[main.cpp](main.cpp) composes [`agc`](../../basic/agc.hpp) from a MOS attenuator,
a transistor amplifier with resistive feedback, a MOS peak detector and an
op-amp error integrator. All feedback acts through connected circuit nodes;
there is no ideal envelope, gain or AGC primitive.

```text
IN --> NMOS series channel --> ATTENUATED --> amplifier --> OUT
           gate = CONTROL          |           ^           |
                                   R           |           |
                                   |      resistive        |
                                  BIAS     feedback        |
                                                           v
CONTROL <-- op-amp + feedback capacitor <-- MOS peak detector
                 REF = detector target         ENVELOPE
```

The attenuator channel connects IN to ATTENUATED; a 10 kOhm return connects
ATTENUATED to BIAS. Increasing CONTROL lowers channel resistance. The amplifier
uses a 30 kOhm / 10 kOhm feedback divider returned to BIAS, requesting gain four
about BIAS before finite-gain/loading effects. The attenuator is nonlinear and
passes positive and negative excursions differently.

A diode-connected NMOS charges a 10 nF envelope capacitor. A 500 kOhm return
discharges it; the nominal release time constant is 5 ms. Its voltage follows
positive output peaks minus the MOS gate/source drop and loading error. It is
not an absolute-value or RMS detector. REF = 1.0 V requests this **detector
voltage**, not a 1 V output amplitude.

The error amplifier PLUS connects to REF. ENVELOPE reaches MINUS/SUM through
1 MOhm, and a 1 nF capacitor connects SUM to CONTROL, the amplifier output.
For sufficient amplifier gain and headroom:

```text
dVcontrol/dt approximately equals (Vref - Venvelope) / (Rinput * Cintegration)
```

This sign raises gain for a weak envelope and lowers gain for an excessive one.
The integrator uses actual feedback charge; its op-amp is the four-transistor
schema from [basic/](../../basic/opamp.hpp). The amplifier's output capacitance,
finite gain, saturation and bias current all remain in the coupled equations.
Analog Devices describes the corresponding detector/integrator/variable-gain
feedback organization in [Measurement and Control of RF Power, Part II](https://www.analog.com/en/resources/technical-articles/measurement-control-rf-power-partii.html).
Our square-wave, low-frequency example is an educational topology, not an RF IC model.

From the package root:

```bash
cmake -S analog -B analog/build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON
cmake --build analog/build --target analog_agc
./analog/build/analog_agc analog/out/agc nominal
./analog/build/analog_agc analog/out/agc-weak weak
./analog/build/analog_agc analog/out/agc-refined refined
ctest --test-dir analog/build --output-on-failure -R 'analog\.(examples\.agc|feedback\.agc)'
```

Arguments are output directory, then scenario. The 3.3 V supply and 1.2 V bias
are explicit `sim_voltage_source` stimuli. A 10 kHz square-wave carrier is
supplied through `sim_driver` with 100 Ohm resistance. Series step sources make
its input amplitude 50 mV, 150 mV at 80 ms, then 50 mV at 160 ms. These are
source amplitudes; connected input loading slightly changes actual IN.

The experiment starts biased: amplifier output capacitor = 1.2 V, controller
output capacitor = 2 V, integrator SUM minus CONTROL = -1 V and envelope = 0.8 V.
These are explicit initial charges. Cold startup is not tested. The nominal run
lasts 240 ms with 5 us steps; `refined` uses 2.5 us steps.

| Late window | Expected observation at nominal step |
|---|---|
| 60–75 ms | Positive OUT excursion about 0.167 V above BIAS; CONTROL around 2.96 V |
| 140–155 ms | Excursion about 0.172 V despite three times the input; CONTROL around 1.75 V |
| 220–235 ms | Excursion about 0.167 V; CONTROL recovers toward 2.94 V |

Every window checks detector range 0.98–1.02 V, positive output peak 1.35–1.39 V,
and the appropriate gain-control range. The stronger input produces a larger
negative excursion: peak control does not promise symmetric swing or constant
RMS. `weak` scales all input amplitudes down by ten and checks that the detector
misses the target while CONTROL approaches its upper swing limit. A pass means
that expected limitation was observed, not that weak-input regulation succeeded.

`trace.csv` columns are time in s, IN, OUT, CONTROL and ENVELOPE in V.
`equations.txt` lists actual devices and wires; `report.json` includes each
trajectory requirement. Square-law MOS omissions, selected initial conditions,
carrier shape and fixed-step integration limit what these tests establish.

The [Verilog-A backend tests](../../VerilogA/tests/README.md) export this physical
hierarchy and compare its measured loop behavior with an independently compiled
OpenVAF model running in ngspice, including native timestep refinement.
