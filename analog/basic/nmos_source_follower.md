# A loaded source follower

[`nmos_source_follower`](nmos_source_follower.hpp) is an `IC` with `IN`, `OUT`
and inherited `Vdd`/`Vss` ports. Include its header directly. One NMOS connects
the supply to the output under input control; a 500 Ω return resistor discharges
the output and establishes continuous bias. Connected loads change its current,
gain, offset and settling through the shared circuit equations.

```cpp
nmos_source_follower buffer;
buffer.IN = amplifier.OUTP;
buffer.OUT = adc.IN;
buffer.Vdd = io_supply;
buffer.Vss = ground;
buffer.elaborate();
```

This wiring illustrates composition; it does not establish that a particular
amplifier and ADC meet their combined operating requirements. The follower
loses gate/source headroom, has gain below one and consumes static current.
Its return resistor limits falling-edge delivery. Calibration and input bias
must account for these effects.

The generic NMOS has β = 0.05 A/V² and threshold 0.7 V. Explicit capacitances
are 20 fF gate/source, 5 fF gate/drain and 20 fF output/ground. In saturation,
the framework supplies

\[
I_D = \tfrac12\beta(V_{IN}-V_{OUT}-V_T)^2.
\]

At DC this current supplies `VOUT / 500 Ω` and the external load current.
The checker solves that KCL independently, using the smaller physical root.
Transient behavior follows capacitor charge and nonlinear KCL. Initial input,
output and supply parameters specify coherent initial capacitor charges;
their default is zero. They are initial conditions, not voltage sources.

`analog.1g.source_follower` independently checks native and Verilog-A trajectories,
then compares them. Nine cases cover all 25 ordered transitions among five
input levels from 2.1 to 2.9 V, 2.97/3.3/3.63 V supplies, 0.05/4 pF loads,
a heavier resistive load, time-step refinement and a 40 pF overload. Normal
cases require delivery within 20 mV by 1.5 ns and DC error below 1 mV by
3.8 ns; overload must fail the delivery requirement. Supply droop and input
charge must be observable. Cross-solver limits are 10 mV over the waveform
and 1 mV at settled plateaus; measured maxima are 6.748 mV and 0.230 mV,
including the overload.

The separate `analog.1g.ihp_source_follower.pvt` experiment replaces the channel
with the IHP four-terminal HV PSP NMOS, explicitly sized at 200 µm / 0.45 µm
and folded into 20 legal fingers. All 90 combinations of five MOS corners,
−40/27/125 °C, three supplies and two capacitive loads pass. Each transient is
checked against its own independently simulated DC sweep, including body
current in supply KCL. PSP body effect and intrinsic charge change the DC
transfer and dynamics; these results do not assert equality with square-law
physics. They do not qualify layout, mismatch or a connected receive chain.

From the repository root:

```sh
cmake --build analog/build --target analog_1g_source_follower
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.](source_follower[.]|ihp_source_follower[.])'
```

Generated model: [`generated/nmos_source_follower.va`](generated/nmos_source_follower.va).
Process binding and retained per-case decks:
[`../1G/IHPSG13G2/follower_process.py`](../1G/IHPSG13G2/follower_process.py).
