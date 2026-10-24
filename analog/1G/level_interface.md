# Digital-to-analog gate voltage interface

[`level_shifter`](../basic/level_shifter.hpp) uses a low-domain CMOS inverter,
two high-voltage NMOS pull-downs, cross-coupled PMOS restoration and two output
inverters. The output buffers isolate the regenerative nodes from external
loading. Its ten transistors, explicit capacitors and finite supply connections
participate in the circuit equations; it contains no controlled voltage source.

```cpp
level_shifter gate;
gate.Vlogic = logic_supply;  // Nominally 1.2 V.
gate.Vdd = analog_supply;   // Nominally 3.3 V.
gate.Vss = ground;
gate.A = control_voltage;
gate.elaborate();
// Connect gate.Y and gate.Y_N to the actual analog gate loads.
```

The generic circuit independently passes C++ and Verilog-A checks at all nine
combinations of 1.08/1.2/1.32 V logic and 2.97/3.3/3.63 V output supply, with
and without a 10 kOhm/200 fF output load. The source includes 50 Ohm input drive
and separate finite supply networks. After independent rail, settling, loading
and overshoot checks, waveforms agree within 11.23 mV RMS and output threshold
crossings within 2.13 ps. The three CTests pass in 786.62 s. Regenerative edges
are compared in time as well as waveform RMS; a peak voltage difference alone
is sensitive to a small edge displacement.

The separate IHP experiment uses actual LV NMOS/PMOS W = 10/20 µm,
L = 0.13 µm; HV pull-downs W = 50 µm; HV latch PMOS W = 5 µm; and HV output
NMOS/PMOS W = 10/20 µm, all HV L = 0.45 µm. Bodies connect to their own supply
domains. All 150 TT/SS/FF/SF/FS, −40/27/125 °C, supply and load cases pass in
975.68 s. The worst sampled high exceeds 95.58% of its supply, the worst sampled
low is 327.2 mV, and the largest supply droop is 71.22 mV.
Those historical PSP numbers used provisional wide single-finger devices and
require a fresh sweep with the corrected geometry binding. The separate
[200 fF cascode clock interface](../basic/cascoded_level_shifter.md) has passed
its corrected-geometry 150-case sweep; this does not transfer that qualification
to the ten-MOS topology.

The tested control changes every 8 ns, with rail checks beginning 2 ns after
each change. This does not yet qualify a 250 MHz comparator clock, pulse-width
distortion, mismatch or routed parasitics. The earlier unbuffered circuit failed
loading/overshoot, and an undersized input inverter failed the slow-corner
settling test. The present circuit still needs integration with the SAR gate
schedule and transistor layout; the SAR slice currently retains host voltage
drivers. PSP intrinsic charge differs from the generic MOS equations, so those
results are separate process-model checks.
