# Physical reference clock multiplier

[`reference_clock_multiplier`](reference_clock.hpp) derives from IC. REF,
CLEAR, OUT and DIVIDED are electrical member ports; Vdd/Vss are inherited.
The selected circuit multiplies a nominal 125 MHz reference to 250 MHz:

```text
REF ──► MOS phase/frequency detector ──► switched charge paths
                    ▲                         │
                    │                    passive loop filter
                    │                         │
                    └── MOS divide-by-two ◄── MOS ring oscillator ──► OUT
                              │
                           DIVIDED
```

The charge paths contain MOS switches and 10 kΩ resistors. A 10 kΩ/2 kΩ/20 pF
lead/lag filter and 1 kΩ/3 pF smoothing section drive a three-stage ring with
50 fF storage per stage. CLEAR resets the divider and detector and clamps one
oscillator stage. Filter charge starts at zero. Output loading and supply
current participate in the shared equations; OUT is not an imposed clock.

The test runs cold startup for 960 ns from a finite 1.2 V supply. Native and
Verilog-A/ngspice each check nominal operation, ±100 ppm reference changes,
20 fF extra output loading, and a 2 pF overload. Normal cases must settle to
the fundamental 2:1 rate within 500 ppm, with at most 80 ps feedback phase
error, 100 ps period variation and 2 mV control ripple. The overload must lose
the target rate and drive control above 0.9 V. A separate cold-start prefix
at half the integration interval checks trajectory convergence.

Both flows pass all six cases independently. Their four normal steady-state
comparisons pass: maximum frequency departure is 31.3 ppm, control departure
1.26 mV and edge departure 5.13 ps, within 200 ppm/3 mV/25 ps limits. These
are selected generic MOS equations. Process fitting, PVT, noise, routed clock
distribution and actual ADC clock loading remain to be qualified.

Each electrical flow also clocks a real C++RTL counter from achieved output
voltage crossings. Native simulation uses hysteretic sensing; the ngspice flow
pauses on the corresponding voltage events and advances the same controller
server. The checker independently recounts both waveforms, so a clock that
fails to reach its rails cannot pass by advancing an unrelated host timer.

This is the local reference path. A slave PHY must derive receive and transmit
timing from its peer's data through a separate recovery loop. This block does
not replace that loop or establish PHY synchronization.

```sh
cmake --build analog/build --target analog_1g_reference_clock
ctest --test-dir analog/build -R 'analog.1g.reference_clock\.' --output-on-failure
```

The model export is [`generated/OneGReferenceClockMultiplier.va`](generated/OneGReferenceClockMultiplier.va).
