# Physical transmitter level shifter

`transmit_translator_layout.py` implements the existing
[`clock_level_shifter`](../clock_level_shifter.hpp) IC. The C++ exporter exposes
VDD, VSS, VLOGIC, BIAS, A, Y and Y_N. BIAS connects to the shared replica in the
complete transmitter; the standalone macro does not contain that replica.

The exporter provides the actual twelve-transistor topology and all declared
storage. The process binding selects the same LV/HV dimensions used by the
core transmitter PSP model. Parallel capacitors combine into nineteen branches;
each becomes a real MIM PCell. This preserves the gate/drain and bias coupling
branches, rather than treating them as independent loads to ground.

Local checks run KLayout DRC and strict LVS and require LVS to reject a removed
transistor. Magic extraction retains distributed resistance and conserves the
capacitance matrix. Minimum-density violations remain explicit for chip fill.
An earlier compact literal-storage candidate occupies 24,967.67 square
micrometres and passes these local checks. Placement prioritizes regenerative nodes, uses deterministic
net ordering and excludes all three supply rails from signal-span cost.

The electrical test supplies each flow separately through finite supply/input
resistance. It checks 0.2 pF and 2 pF output loads over sixteen 8 ns intervals
after 32 ns startup. Each waveform must independently meet the existing
level-shifter rail, settling, overshoot and supply-loading contract. Every LV
transistor's gate-to-drain/source/body difference must remain at most 1.65 V.
Only then are edge sequences and waveforms compared, with 100 ps and 150 mV RMS
layout budgets. The 100 ps allowance is 1.25% of the symbol interval, separate
from the tighter numerical agreement required for identical solver equations.

The first layout passes both independent 0.2 pF electrical checks but fails
comparison at 308.3 ps and 248.3 mV RMS. The compact literal layout also fails
comparison, at 460.5 ps and 326.3 mV RMS. Tests use a finite 1.8 V external bias source.
Shared-replica startup, connected steering devices, full TX, PVT, fill and
complete-chip verification remain separate requirements.

An explicit `--storage psp-charge` trial selects a different implementation of
this whole IC. PSP supplies intrinsic charge in place of the base class's
22 transistor-charge approximation elements; both 200 fF output-compensation
capacitors declared by `clock_level_shifter` remain real MIMs. Source provenance
and port/value checks guard that boundary. This is a functional process
realization, not identical-equation lowering or a general rule for deleting
capacitors. The literal storage implementation remains available. The new
variant is under qualification and is not yet the CTest default.

The PSP-charge candidate occupies 14,272.74 square micrometres and passes
local geometry, connectivity and extraction checks. Its two flows pass the
absolute electrical limits at both loads, but comparison fails at 236/310 ps
and 237/218 mV RMS. Diagnostic removal of wiring resistance brings both loads
within the comparison limits; removing only wiring capacitance does not.
Idealizing only the ground network brings the light-load case to 91 ps and
108 mV RMS. These artificial networks locate a cause and are not physical
implementations or qualification evidence.

Widening all signal buses to 1 µm worsened the comparison and was reverted.
Direct supply columns above the actual transistor sources pass local checks
without increasing the footprint and reduce differences to 143/185 ps; they
still fail the delay limit. Distributed source-stripe Via1 and Via2 arrays,
local Metal3 collectors, eight-cut supply columns and upper supply/output
rails now pass local geometry and nominal electrical verification. The
0.2/2 pF comparisons are 85.7/97.0 ps and 103.5/90.6 mV RMS. Both flows also
pass all independent electrical limits. The 100 ps/150 mV limits are unchanged.

The explicit PSP-charge realization has its own `translator_psp_layout` and
`translator_psp_mos_tt`, `translator_psp_mos_ss`, `translator_psp_mos_ff`
CTests. They use 27, 125 and −40 °C respectively, nominal passive models and
nominal supplies. The first maintained matrix passes TT and FF. At SS/125 °C,
the light load passes at 98.75 ps but the heavy load fails at 111.90 ps.
Both independent electrical checks pass. Subsequent routing changes reach 105.18 ps at the heavy SS load, still above
the unchanged limit. Center-fed supplies, source-via arrays and output routing
are present in that candidate. Private drain via stacks are the next local
geometry and electrical trial. The subsequent local upper-route candidate passes SS/125 °C at both loads:
92.98/97.68 ps and 89.40/77.34 mV RMS. Inverter-output wire capacitance drops
from 22.01 to 14.33 fF; both local pull-down/cascode connections also avoid the
long bottom bus routes. The literal-storage variant retains its additional
storage connections and the original routing of those nets. The maintained
six-test guard/geometry/corner regression passes in 874.28 s. These tests do not
establish supply/passive corners or full TX.

`transmit_translation_bias_layout.py` builds the separate shared replica from
a C++ export of `level_shift_bias` with the transmitter's actual 50 pF setting.
It retains the three resistors, HV diode-connected MOS and ten physical MIM
plates. Its own startup test uses finite supply impedance and checks both
circuits independently before settled-bias comparison. The initial schematic
incorrectly merged the custom wafer and contacted VSS nets; LVS rejected it.
The corrected candidate retains the row builder's explicit substrate and tap
devices. Two-cut capacitor escapes fix the first layout's spacing violation.
Local DRC/LVS, missing-replica rejection and all three startup corners pass:
the physical settled bias is 1.768–1.855 V and differs by at most 1.33% from
the independent schematic, below the 5% local budget. Each waveform must
reach 2 µs, retain startup samples, settle within 1 mV over the last 400 ns,
and meet the transmitter's existing 1.2–2.3 V bias-headroom requirement.
The maintained `translation_bias` and `translation_bias_guards` CTests both
pass, including the startup-completeness guard (116.94 s total).
The connected `translator_replica_mos_tt`, `translator_replica_mos_ss` and
`translator_replica_mos_ff` tests replace the ideal bias source with this
physical replica and its independent schematic. Both flows run through 2 µs
startup and 128 ns switching with a 2 pF load and finite supply impedances.
Each separately checks output behavior, bias headroom and every LV gate's
terminal differences before comparison. Six negative/positive guard cases
cover missing startup, missing bias, oxide exposure, bias loss and conflicting
upstream hashes. The initial TT diagnostic passes at 94.32 ps and 86.96 mV RMS;
the maintained three-corner results after the storage-route fix are below. This is one translator with
ideal macro-to-macro wires; the full 22-translator bank remains unqualified.

```sh
cmake --build analog/build/regeneration-audit --target analog_1g_ihp_transmit_translator_export
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog\.1g\.ihp_transmit\.translator_' --output-on-failure
```

The geometry fixture uses the established sidewall-corrected Magic build.
The experimental nine-significant-digit formatter serializes a nominal
0.45 µm HV channel as 0.449999988 µm; the existing model-range guard rejects
that representation. Its failed receipt is retained. No geometry threshold,
extraction coefficient or conservation limit was changed for this macro.
The separate double-precision geometry-scale/output build passes its 102-value
C regression and actual translator extraction, including minimum-length device
range checks. Current diagnostic layouts use `out/magic-double-scale`. This
serializer fix does not change extraction coefficients or acceptance limits.

The maintained connected test passes TT and FF but initially fails SS/125 °C:
106.48 ps maximum edge difference, despite both absolute contracts passing.
The replica's original capacitor access travels around the full bank perimeter.
`transmit_translation_bias_routes.py` adds direct TopMetal1 access to the BIAS
plate and a separate TopMetal2 ground crossover to the bottom plate. Its local
DRC/LVS and three startup corners pass without changing the 75,638.83 µm²
footprint. Connected SS then passes at 96.53 ps and 74.54 mV RMS; physical bias
excursion falls from 154.52 mV to 36.45 mV. Both maintained replica CTests pass
in 61.38 s. All four maintained connected CTests then pass in 1,278.57 s:
TT 83.52 ps/78.15 mV RMS, SS 96.53 ps/74.54 mV RMS, and FF
75.13 ps/72.25 mV RMS, plus the evidence guards.

`transmit_translation_bank_export.cpp` elaborates the complete cascoded TX,
then exports its 22 actual translator ICs and shared replica through checked
ownership boundaries. It rejects private connections outside an IC, aliased
ports and changed device counts. `transmit_translation_bank.py` additionally
checks that each exported translator matches the selected physical macro.
The bank experiment switches all inputs simultaneously with separate 2 pF/
10 kΩ output loads and the established finite TX supply impedances. Each output
must separately pass the existing contract before the two flows are compared.
An isolated SPICE observation source records every LV finger's worst gate
terminal difference, keeping startup traces bounded in size. Seven maintained
guards pass, including a SPICE check of that observer and a startup exposure
failure confined to the last IC. Bank electrical runs remain pending; these
loads do not replace the actual steering/trim circuits or routed integration.
