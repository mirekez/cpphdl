# Delivered reference clock

[`reference_clock_domain`](reference_clock_domain.hpp) is an `IC` that connects
the physical [reference multiplier](reference_clock.md), a CMOS buffer pair, a replica bias and the
[corrected voltage interface](../basic/cascoded_level_shifter.md). It takes one
external 125 MHz reference and produces a nominal 250 MHz, 3.3 V clock.

`Vlogic` supplies the multiplier and low-voltage translator gates. Inherited
`Vdd` supplies the replica bias and high-voltage translator devices. Both share
inherited `Vss`. `REF` and `CLEAR` use the low-voltage domain. `OUT` and `OUT_N`
are the translated clock and its complement. CLEAR resets the multiplier;
qualification includes bias settling and release from that declared startup.

The translator's input is wired to a two-stage CMOS buffer. The initial direct
connection exposed excess oscillator loading and a delivered-clock rail failure.
The buffer presents 5 fF of explicit input/feedback charge to the oscillator and
supplies the larger translator gate charge from `Vlogic`. Both inverter stages
retain their own gate, feedback and node charge. An additional 5 pF local logic
decoupling capacitor supplies transient current. Output loading still acts back
through the buffer, shared bias and finite supply impedances.
No source forces the delivered clock to meet its frequency or voltage purpose.

The integration experiment starts with discharged capacitors, finite 1.2/3.3 V
supplies, the reference clock and reset. It checks nominal and 200 fF output
loads, a 20 pF overload and a halved integration step. The internal PLL must
meet its 2:1 frequency, phase and control-headroom contract. The delivered clock
must settle below 0.33 V and above 2.8 V within the declared 1 ns delivery
budget. The overload must break that rail purpose while leaving its effect in
the physical equations.

A real C++RTL counter advances from OUT's achieved 0.99/2.31 V hysteretic
crossings in both flows. Native simulation uses the clock adapter; ngspice
pauses on voltage events and drives the same controller server. The checker
independently recounts each flow's waveform. Solver comparison and refinement
allow at most 80 ps in startup edges and 5 mV in control voltage. At 10/5 ps,
the native first startup edge moved by 92 ps, while control moved by less than
1 mV. Verilog-A refinement passed. The final 5/2.5 ps runs retain the same bounds
and pass all three CTests in 3378.66 s
(`analog/out/oct21-reference-clock-domain-ctest.log`). Native refinement moves
prefix edges by at most 15.71 ps and control by 0.434 mV; Verilog-A refinement
moves them by 0.112 ps and 0.680 µV. Both nominal controllers count 164 achieved
output edges. The largest cross-solver departure is 37.0 ps in startup edges
and 0.935 mV in control voltage, including the overload case.

```sh
cmake --build analog/build --target check-1g-reference-clock-domain
```

The multiplier uses selected generic MOS laws. The translator separately
passes its actual-IHP process sweep, but that does not qualify the combined
PLL in IHP, a complete ADC bank, a recovered-data loop or a routed clock tree.
