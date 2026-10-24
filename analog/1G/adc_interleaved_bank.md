# A smaller interleaved SAR controller

`InterleavedSARBankController<Slices>` launches one conversion per controller
clock and exposes each slice's actual acquisition, trial-code and conversion
controls. Ports are class members. `_assign()` binds each child's ports once;
`_work()` computes next state and `_strobe()` commits it. A one-hot launch
register avoids a binary counter with a comparator at every slice.

The existing eight-bit SAR sequencer has this schedule:

| Clock after launch | Activity |
| --- | --- |
| 0 | Accept start |
| 1–2 | Track input |
| 3 | Open sampler; separate sampling from reference switching |
| 4 | Apply first DAC trial |
| 5–12 | Accept eight comparator decisions |
| 13 | Parent emits the completed result; this slice can accept its next start |

At 250 MHz, tracking lasts 8 ns. Thirteen slices can therefore maintain one
result every 4 ns after pipeline fill. Twelve slices attempt reuse before the
SAR returns idle; the controller reports a sticky collision fault. A thirteen-
slice bank has 18.75% fewer sampling/conversion slices than a sixteen-slice
bank. This is a count reduction, not a measured routed area reduction.

The native regression checks 4,096 continuous launches followed by a
deterministic sequence of request gaps in thirteen- and sixteen-slice banks against independent expected codes, slice identities, exact result
cadence and acquisition/conversion masks. A twelve-slice negative case must
raise a collision. The RTL regression repeats the same contracts for all three
sizes and compares complete native and RTL trajectories. Conversion uses the
same root C++HDL entry point as other digital modules; Verilator overrides the
numeric `Slices` parameter for each case.

The default analog receiver still uses sixteen slices. A thirteen-slice cold-start
differential receiver is under electrical qualification. Reducing the default requires separate
closed electrical tests of sampler reuse, settling, decision delivery and
conversion accuracy, plus routed 250 MHz timing. No analog area saving is
accepted merely because the digital schedule fits. The older fixed-size
controller remains available while that integration is unfinished.

Build `analog_1g_adc_bank` and run `analog.1g.adc_bank.native`. The maintained
RTL runner is `tests/adc_bank_rtl.py`; its receipt records source/tool hashes,
all three traces and comparison results under the selected work directory.


IHP standard-cell mapping repeats the independent functional test and compares
all 8,256 clocks per profile. The thirteen-slice controller uses 37,585.296 µm²
of LEF cell footprint, versus 45,715.6224 µm² for sixteen slices: a 17.785%
controller cell-area reduction. The runner records both Liberty area and LEF
footprint because the pinned library gives slightly different annotations for
`sg13g2_a22oi_1`. These are cell sums, before placement, clock trees and routing;
they do not establish 250 MHz timing or the analog receiver's final area.
`tests/adc_bank_mapped.py` records the mapped netlists, the reconstructed array
port interface, complete trajectories and hashes of its native/RTL prerequisite.

The standalone thirteen-slice raw controller also has a recorded 400 × 400 µm
layout (0.16 mm²). Its native/post-route trajectories match exactly, extracted
setup/hold slack is +1.24/+0.08 ns, all 868 enabled DRC categories pass, and
strict transistor LVS passes. This excludes the normalizer, startup sequencer,
analog converters, translators and full-chip wiring. Post-fill coupling is
not extracted. The corrected 360 × 360 µm result also passes exact functional comparison,
all 868 DRC categories and strict LVS, with +1.10/+0.06 ns extracted slack.
Its 0.1296 mm² layout is 19% smaller. See the
[physical flow](IHPSG13G2/adc_bank_physical.md) for the local correction and scope.

For controlled startup, see [the composed receive-lane controller](adc_receive_lane.md).
