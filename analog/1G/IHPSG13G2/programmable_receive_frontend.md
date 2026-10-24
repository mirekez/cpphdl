# Buffered programmable receiver

The current candidate uses 1.5 pF sample storage, with reviewed PSP widths
25/50 µm for the sampling NMOS/PMOS and 12.5/25 µm for their dummy devices.
The 25 µm devices use five 5 µm fingers, compatible with the PCell grid.
All four selected sampler/dummy layouts pass strict MOS/tap LVS and local
DRC, antenna and pin checks; see [MOS layout](mos-layout.md).
Compared with the preceding 3 pF, 100/200 µm sampler, storage is halved and
summed sampler MOS width is reduced by 75%. These are component dimensions;
verified layout and die area must be measured separately. The real PDK
[capacitor test](capacitor-layout.md) measures a summed 0.13183 mm² storage
footprint saving across eight sixteen-slice lanes; it does not include routing
or establish die savings.

The nominal single-slice closed-conversion tests pass independently in
C++RTL, Verilog-A and IHP PSP. C++RTL and Verilog-A return the same ADC code.
Verilog-A also passes 32 conversions across all sixteen slices at both gain
extremes. Native C++RTL and IHP PSP bank checks and complete acquisition
experiments are still required before this candidate is qualified. PVT, noise and mismatch remain
open. The experiments below distinguish historical circuits from this candidate.

The separate single-slice process/temperature checks pass at nominal TT/27 °C
and FF/−40 °C, at both gain extremes and nominal supply voltages. SS/125 °C
passes the lower gain, but fails high-gain transfer and linearity: measured gain
−6.11948 against the intended −8 (±20%) and 21.042 mV residual against 15 mV.
Acquisition still passes at 16.965 mV. These failures remain unresolved.
Small-signal AC diagnosis gives approximately −7.59 at 1 MHz in this corner,
pointing to insufficient dynamic settling as a cause. Compensation and bias
experiments are separate from the maintained circuit and do not qualify it.
These checks do not include supply extremes, mismatch or cold startup.

Reducing switch width alone with 3 pF storage failed the full-array PSP
acquisition limit: 25.800 mV against 20 mV, despite hold variation improving
to 68.137 µV against 10 mV. Stronger output buffers reduced tracking error but
increased device width. A 1 pF alternative passed the first sixteen acquisition
windows but failed closed-conversion hold variation at 21.778 mV against 20 mV.
The selected 1.5 pF candidate passed those initial acquisition windows at
18.673 mV and the single-slice closed-conversion checks. Complete bank tests
use the same sampling reference and limits; the aperture is not fitted to the
observed waveform.

`programmable_receive_frontend<Count> : IC` connects a fixed 100 Ω differential
termination, two parallel differential amplifier/current-buffer paths, one
[programmable gain stage](programmable_receive_gain.md), and the interleaved
SAR ADC. Each declared device contributes to the shared electrical equations.
The ADC's sampling current returns through the gain stage, its feedback string,
the current buffers and the supplies.

The high-impedance differential amplifier precedes the programmable stage.
Its resistor string presents a gain-dependent input impedance; connecting that
string directly to cable pins would change the termination. The current buffers
isolate it from the 100 Ω input. One gain stage also removes 20 MOS devices and
associated bias/compensation passives compared with separate gain stages on
both input legs. Pad capacitance, ESD, magnetics, hybrid/echo paths and package
parasitics still need integration around these input ports.

`PLUS` and `MINUS` are the terminated differential input. `SELECT_N[0..4]`
select the same ordered, inverting gains as the standalone stage. They are
physical analog voltages; the core gain controller requires real level
translators before these ports. `Vlogic`, `EVALUATE`, `CLEAR`, `TRACK`, `BIT_N`
and `CORE_DECISION` retain the ADC's explicit supply, clock, control and result
connections. Training/polarity handling must include the gain-stage inversion.

The module fixture uses the declared sample storage, 50 Ω source impedances on both
input legs, finite supply impedance and finite control-drive resistance. It
warms the declared precharged state for 16 µs, then sends 16 PAM5 symbols at
125 MBd through each gain setting. Acquisition lasts 2.5 ns and closes 6.6 ns
after each symbol boundary; the fixed observation aperture is 7.0..7.8 ns.
This phase accounts for propagation through the amplifiers and gain stage.
It is the same in both solvers and all settings. It does not establish a
recovered-clock implementation or cold-start qualification.

Before this switch-size reduction, both generic solvers passed the absolute gain, 15 mV linearity, fixed-termination,
20 mV acquisition, headroom and rail checks at all five settings. The production
native signal step is 2 ps; Verilog-A uses a 20 ps maximum step. Separate full
transient waveform comparison passes the unchanged 10 mV bound at every setting.
The largest observed-node differences are 3.996, 4.152, 4.324, 4.725 and 5.655 mV
in ascending gain order. The earlier 20 ps native experiment failed this
comparison during fast sampling edges; refining the integration step resolves
that numerical difference without relaxing the electrical checks.

For the preceding wide-switch 3 pF circuit, the nominal PSP flow binds 85 MOS devices to reviewed IHP widths and
lengths, retains all 105 declared capacitors, checks all 66 resistor and 85 MOS
contributions against the actual generated topology, and folds devices into
valid fingers. That nominal process regression passed all five settings
after the native-step refinement: gain magnitudes 0.78103, 1.21208, 1.90875,
3.20838 and 7.05859; worst acquisition error 13.697 mV and worst linearity
residual 13.330 mV. The exported process experiment retains its 20 ps maximum
step. PSP is a different MOS model from the square-law generic flows;
this is an independent electrical-purpose check, not waveform equivalence.

Nine single-slice binding tests reject unknown owners/geometry, altered buffer connections,
wrong termination, missing/negative storage and swapped supply observations.
Eight checker tests include serialized C++/SPICE comparison fixtures, polarity,
acquisition-error cancellation, missing excitation/windows, collapsed rails,
stuck tracking and missing provenance.

```sh
cmake --build analog/build --target analog_1g_ihp_programmable_receive_frontend
ctest --test-dir analog/build --output-on-failure \
    -R '^analog[.]1g[.]ihp_programmable_receive[.]' -j 1
```

`generated/OneGIHPProgrammableReceiveFrontend.va` exports the sixteen-slice
hierarchy, including all five select ports. The separate
`analog.1g.ihp_programmable_array.*` tests exercise its lowest and highest gains
with 64 acquisitions at 250 MS/s: four acquisitions on each of sixteen physical
samplers. They check capture against the input at a fixed falling-clock
midpoint, subsequent hold variation, gain, termination, headroom and both rails.
The Verilog-A flow partitions compilation and solves the complete connected
network in one ngspice instance. For the preceding wide-switch 3 pF circuit, the sixteen-slice Verilog-A flow passed both settings: acquisition errors
3.262 / 3.542 mV and hold variation 18.588 / 16.673 µV. The preceding native array
passed both settings: acquisition 3.279 / 3.587 mV and hold variation
18.985 / 17.158 µV. Its separate waveform comparison also passed, with maxima
7.445 / 7.447 mV below the unchanged 10 mV limit.
That full sixteen-slice PSP experiment failed the unchanged acquisition and
hold-variation limits at the lowest gain: 30.050 mV and 20.516 mV respectively.
Halving the warmup timestep reproduces both failures. The original complement
driver turns the large sampling PMOS off too slowly; the current candidate reduces switch width and storage as described above. A passing single-slice
PSP result does not qualify this full array. These acquisition tests
do not perform ADC bit conversions. Physical control/clock translation,
closed-loop gain acquisition, full-bank conversion, PVT/noise and layout remain
required.

## Closed conversion

`analog.1g.ihp_programmable_conversion_{1,4,16}_gain{4,8}` connects the actual
SAR controller to each electrical slice. Native C++RTL, generic Verilog-A and
IHP PSP each consume their own comparator decisions. The testbench supplies
finite-resistance control drivers; physical control translators remain required
in the IC. Simulation sensing is confined to the testbench.

Each flow checks acquisition within 20 mV, held-conversion variation within
20 mV, ADC error within two counts, comparator stability, supply/headroom,
conversion cadence and normalization. The hold bound includes DAC/comparator
kickback and is distinct from the acquisition-only fixture's 10 mV hold bound.
The comparator must remain binary throughout its observation aperture, including
all repeated-time simulator states. A separate comparison requires independently
passing generic flows and at most one ADC count difference. PSP uses another
transistor law and must pass the same electrical purpose independently.

These tests measure conversion of the achieved electrical input. They do not
establish ideal DC gain, recovered sampling phase, full PHY operation or signoff.
