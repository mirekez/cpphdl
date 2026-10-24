# Core-controlled receive DAC

`core_receive_dac : IC` accepts eight positive-polarity `TRIAL` ports in the
core voltage domain and produces an analog threshold at `OUT`. `Vlogic`
supplies the core input stages; inherited `Vdd` and `Vss` supply the high-voltage
stages and resistor ladder.

One physical replica bias supplies eight cascode level shifters. Their
complementary outputs drive the eight CMOS rail drivers in an R/2R ladder.
Changing one trial bit changes transistor currents, shared bias charge,
rail loading and the achieved threshold. The ideal threshold is
`Vdd * trial / 256`; transistor resistance, loading and settling produce
measurable departures from it. The class contains no simulation drivers.

The candidate uses a 25 pF shared bias capacitor and a 750/1500 Ω ladder.
Compared with the earlier receiver ladder, its NMOS/PMOS rail-driver widths
are halved to 100/200 µm. Translator latch widths increase to 40 µm and output
inverters use 40/80 µm. The explicit process binding selects these geometries;
it does not calculate a PDK width automatically from a generic channel beta.
All wide devices are folded into legal PDK fingers. This is a sizing study,
not a routed-area result.

The unit fixture starts stored charge at zero, ramps both supplies, waits
8 µs, then applies sixteen words at a 4 ns period. They include full-scale
reversals, individual bits, alternating bits and the major-carry transition.
Core commands have 100 ps edges and 50 Ω source impedances. Supply impedances
are 0.25 Ω and 10 Ω, with 20 pF decoupling on each rail. A real 100 fF capacitor
loads `OUT`; the transistor ladder gates load the level shifters directly.

The native backward-Euler solver uses 50 fs steps for the first 1.5 ns
following each word, then 2 ps steps through the settled interval. SPICE uses
a maximum 0.5 ps measurement step. Both preserve the continuously integrated
warmup state; the runner verifies the actual observed timestep coverage.
The finer native switching step follows measured convergence, not a change
to the circuit, excitation or acceptance limits.

Every flow must independently satisfy the same checks:

- At most two counts of threshold error throughout 1.5–2.0 ns after each word.
- Correct core commands and translated rails during that complete aperture.
- Supply, bias and output bounds throughout the observed interval.
- At most 1.65 V across the checked core-device gate/source/drain differences,
  including the cascode's low-voltage nodes, throughout the observed interval.
- Complete observation coverage and full-scale excitation.

C++RTL and generated Verilog-A must also agree within 5 mV on both simulators'
observed grids, using linear interpolation of the other trajectory. A negative
regression places a spike between native observations: comparison on the
native grid alone misses it, while the reverse direction rejects it.
IHP PSP uses its own transistor equations and the same absolute
contract. The maintained process cases are TT/27 °C, SS/125 °C and FF/−40 °C.
Checker regressions reject missing apertures, individual bad states and the
last bit's incorrect controls. Binding regressions reject altered topology,
missing or precharged storage, wrong exposure probes and unknown devices.

Build `analog_1g_ihp_core_receive_dac` and run CTest with
`-R '^analog.1g.ihp_core_receive_dac\.'`. The flow names are `cpp`, `veriloga`,
`compare`, `nominal`, `ss_hot` and `ff_cold`; `contract`, `comparison_guards`
and `bindings` check the verification infrastructure. Each run preserves its source/tool hashes,
exported equations, process deck, waveform and individual checks.

The complete refined sixteen-word runs pass all absolute checks, including
input-PMOS drain-to-core-supply exposure:

| Flow | Maximum aperture error, counts | Maximum checked core-device exposure |
| --- | ---: | ---: |
| C++RTL | 0.492464 | 1.361199 V |
| Verilog-A | 0.491783 | 1.361251 V |
| IHP TT, 27 °C | 0.953715 | 1.422254 V |
| IHP SS, 125 °C | 1.889146 | 1.437201 V |
| IHP FF, −40 °C | 0.672082 | 1.390972 V |

The comparison on both grids also passes: maximum difference **3.192400 mV**
against 5 mV, across 500,000 native and 640,473 Verilog-A observations in the
common interval. Receipts are under
`build/1G/IHPSG13G2/tests/results/core_receive_dac_refined/`, with slow/hot and
fast/cold results in sibling `_ss_hot` and `_ff_cold` roots.
`comparison_both_grids.json` records each direction, receipt hashes and the
checker hash.

The original translator/ladder combination failed SS settling at 31.893
counts. Output strengthening alone still failed at 22.825 counts, and an
oversized latch opposed the pull-down. Those rejected circuits remain
recorded. The selected circuit's initial coarse native integration passed
settled checks but failed waveform comparison at 119.577 mV. Refining native
steps from 2 ps to 0.25 ps and then 50 fs demonstrated convergence before
the complete reruns above. The failed reports and source snapshots remain
retained; neither circuit stimulus nor acceptance limits were relaxed.

Nominal PSP rail-source energy over this sixteen-command fixture is
146.4 pJ per command, averaging 36.60 mW over 64 ns. This follows integration
of `Vsource * (Vsource - Vrail) / Rsource` on the two finite supplies. It
includes rail-decoupling energy changes and excludes external command-source
energy and startup. It is a unit loading characterization, not a full-bank
power estimate.

These unit tests do not establish comparator settling, closed-loop ADC
accuracy, alternate power sequencing, mismatch or extracted performance.
The complete receiver still requires the actual connected control interface.

See the [connected timing budget](receive_timing_budget.md) before reusing
the unit's settling result in a full lane. Controller delay, physical clock
skew and preamplifier settling must share the available half-period.
