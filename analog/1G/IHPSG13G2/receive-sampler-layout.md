# Physical receive sampler

`tests/receive_sampler_export.cpp` elaborates the actual
`cold_charge_balanced_receive_slice`, then selects its sampler and complement
inverter. Every connection to the remaining parent circuit must be an exported
port. The resulting five-port module retains all eleven contributions: six
MOS channels and five storage branches. No independently copied sampler
parameters configure this export.

`receive_sampler_layout.py` checks the exported owners, terminal orientation,
channel strengths, storage and cold initial state before creating PDK cells.
The dimensions match the reviewed full-receiver process binding:

| Role | Type | Total width | Length | Fingers |
| --- | --- | ---: | ---: | ---: |
| Complement pull-up | HV PMOS | 20 µm | 0.45 µm | 2 |
| Complement pull-down | HV NMOS | 10 µm | 0.45 µm | 1 |
| Main sampling switch | HV NMOS | 37.5 µm | 0.45 µm | 5 |
| Main sampling switch | HV PMOS | 75 µm | 0.45 µm | 10 |
| Charge-cancellation dummy | HV NMOS | 20 µm | 0.45 µm | 2 |
| Charge-cancellation dummy | HV PMOS | 42 µm | 0.45 µm | 5 |

The 1.5 pF hold capacitor remains MIM. The 20 fF complement load, both 2 fF
clock couplings and 1 fF input coupling also remain explicit. Legal series
MIM cells implement the small coupling values; their intermediate nodes and
parasitics remain in extraction. The dummy transistors retain their intrinsic
PSP charge even though each has its drain and source connected together.

Local DRC and strict LVS pass for the current 15,243.42 µm² candidate.
LVS checks 21 normalized devices, including substrate taps and nine physical
MIM capacitors. Removing the hold capacitor, NMOS dummy or PMOS dummy is
independently rejected. Isolated minimum-density findings remain; this is
not filled macro or whole-chip DRC acceptance. The MIM drawing area is
1,024.3521 µm² per sampler.

The initial 2 µm signal-route candidate passed those geometry checks but
failed electrical comparison. Both circuits passed their static acquisition
and held-voltage limits at five input levels; maximum waveform departure
during closing reached 27.659 mV against a 5 mV budget. A second candidate
used 0.24 µm signal routes with parallel via cuts and retained the broad supply
rails. That candidate also failed comparison, reaching 34.110 mV during
closing despite passing both independent static/hold checks. The earlier
failed results and source snapshots remain in the recovery directory.

Separate diagnostics retain the original device models while removing wire
capacitance or reducing wire resistance. At 1.85 V, comparison departure is
17.051 mV with near-zero wire resistance, 25.947 mV without wire capacitance,
and 5.713 mV with both changes. These modified circuits are diagnostic only.
They motivated a third candidate with grounded Metal5 shields between the
signal buses and two external Metal4 return columns. Its 0.26 µm tracks leave
clearance for 0.20 µm shields and the existing via landings. That layout
passed geometry checks but failed comparison at 46.101 mV. The current
fourth candidate retains these shields and moves the physical output pin
to the hold capacitor's TopMetal1 plate, so the downstream load connects
there. Geometry and missing-device rejection pass. The complete electrical
comparison still fails at 28.042 mV; independent acquisition and hold checks
pass, with maximum held-target error 12.649 mV. No candidate has passed the
complete sampler unit contract. Neither wiring mechanism is removed from
acceptance simulations.

Further isolated routing trials retain all devices and intentional storage.
Clustering the complement inverter with its loads and moving CLOCK_N onto
TopMetal2 reduces peak comparison error to 23.873 mV, still above 5 mV.
Adding separate upper-metal VDD/VSS paths to the complement inverter's source
escapes passes local DRC/LVS and missing-hold/dummy rejection in 71.18 seconds.
It increases area to 19,181.72 µm², but the five-level electrical CTest still
fails in 132.18 seconds: peak comparison error is 23.743 mV. Both independent
flows pass their acquisition/hold checks; maximum physical held-target error
is 14.666 mV. The small improvement does not justify selecting that larger
layout. The fourth candidate remains frozen for the separately queued parent
conversion diagnostic; it remains unqualified.

An isolated C++ sizing trial changes only the complement inverter's two
channel-strength declarations from 0.004 to 0.016 A/V², binding them to
N40/ng4 and P80/ng8 at L=0.45 µm. All other nine sampler laws remain unchanged.
Its layout passes local geometry and missing-device checks. The five-level
electrical trial uses the corresponding 34 extracted MOS fingers and the
original voltage/time limits. Independent acquisition/hold checks pass, but
comparison still fails at 21.897 mV; maximum held-target error is 11.017 mV.
Increasing inverter width alone therefore does not resolve the closing error.
The maintained C++ headers remain unchanged.

Nonloading terminal probes on that sizing trial reproduce the original
waveforms. At the worst 1.1 V closing point, the physical inverse-clock gate
is 1.818 V versus 2.780 V in the schematic. The complement PMOS supply
terminal is 3.145 V while its external supply is 3.294 V. This motivates
repairing local access resistance before increasing transistor width again.

An isolated source-contact trial adds 20 Via1 cuts per source stripe,
continuous Metal1/Metal2 strips and distributed Via2 access, preserving the
same transistor sizes, storage and 15,672.47 µm² footprint. The initial
discontinuous landings fail Metal1 spacing; continuous strips pass local
DRC/LVS and missing-device checks in 73.25 seconds. Full five-level extracted
testing still fails in 141.64 seconds: peak comparison error improves to
19.678 mV but remains above 5 mV. This is a rejected electrical candidate,
not an adopted sampler or a relaxed acceptance limit.

Direct upper-metal source/drain access additionally passes local DRC/LVS
after correcting TopVia2 spacing, but increases area to 19,623.42 µm² and
barely changes peak mismatch (19.667 mV). It is not selected.

A separate [junction-geometry audit](analog-extraction.md#mos-junction-geometry)
finds that Magic reports half the actual diffusion area for both dummy
transistors. The maintained optional correction derives each finger's area
and perimeter from GDS and its extracted terminal positions. Six guards pass
on both baseline and stronger-driver geometry. Full electrical retests still
fail comparison: 28.018 mV for the baseline and 19.681 mV for the source-contact
candidate. Both retain passing independent acquisition/hold checks. The
correction addresses physical-model accounting without changing acceptance
limits or the queued parent conversion's frozen inputs.

Selective wire diagnostics on the corrected sizing candidate identify direct
clock routing as a larger contributor than the inverse-clock trunk: reducing
only direct-clock wire resistance to 0.001× gives 17.497 mV error, and removing
only its 86.61 fF of wire capacitance gives 15.701 mV. These deliberately
modified circuits are diagnostic, not qualification evidence. Moving the
hold capacitor after the clock-driven device group then passes local geometry
in 70.13 seconds at the unchanged 15,672.47 µm² area. Its full, unmodified
extracted circuit improves to 17.074 mV peak error but still fails the 5 mV
comparison (127.46 seconds); both independent flows pass their checks.

A subsequent direct-clock route uses a center-fed TopMetal1 input trunk,
TopMetal2 posts and separate crossings from the inverse-clock trunk. It
removes the old direct-clock Metal4 posts and Metal5 trunk. Local DRC/LVS and
missing-device checks pass in 67.99 seconds at the same footprint. Full
electrical comparison fails in 126.97 seconds, reaching 20.186 mV. A separate
time-window audit finds maximum held-sample differences of 5.946 mV for the
baseline, 7.118 mV for the compact placement and 3.647 mV for this upper-clock
route. The latter improves held accuracy while worsening the closing peak.
All original acceptance windows remain unchanged; neither variant qualifies
the sampler, and actual ADC conversion with these variants remains unverified.

A further isolated C++ candidate scales the four main/dummy switch strengths
to 0.32 of their original values while retaining the stronger complement
inverter and all five storage branches. Its physical main switches are
N12/ng4 and P24/ng8; its dummies are N6.4/ng2 and P13.44/ng4, all at
L=0.45 µm. The first attempted quarter-width geometry was rejected because
its finger widths were unsupported; the legal 0.32 sizing passes local
geometry in 68.64 seconds at 15,482.61 µm². Full five-level electrical testing
still fails, but peak mismatch falls to 8.353 mV. Combining those sizes with
the upper direct-clock route passes geometry in 65.98 seconds and reaches
7.665 mV maximum mismatch, with 4.086 mV maximum held-window difference.
Both circuits independently pass every original acquisition/hold check;
the full 5 mV comparison still fails. These are sizing diagnostics, not
qualified replacements for the frozen baseline or its parent conversion.

Reducing those four switches to 0.20 of their original strengths gives
N7.5/ng3, P15/ng6, N4/ng2 and P8.4/ng4. With the upper clock route, local
geometry and missing-device checks pass in 109.43 seconds. The complete
five-level electrical test fails in 119.92 seconds: comparison errors are
5.190, 4.958, 4.878, 5.289 and 5.676 mV at increasing input levels. Both
independent circuits pass every acquisition/hold check. Only the two middle
levels pass comparison; the candidate as a whole remains unqualified.

Reducing the complement driver to N20/P40 and then N10/P20 does not improve
the worst comparison: the complete tests reach 6.390 and 8.349 mV. Removing
all obsolete lower-metal inverse-clock posts reduces that clock's wire
coupling into OUT from 7.308 to 3.796 fF, but still gives 6.669 mV peak
comparison error. These layout/driver variants are rejected.

Keeping total switch widths unchanged while reducing their segmentation
also passes geometry. Fewer fingers on all four sampling/dummy devices gives
5.669 mV maximum mismatch at 14,863.70 µm². Reducing only the PMOS main/dummy
devices to two/one fingers gives 5.519 mV at 15,005.95 µm²; its geometry and
electrical CTests finish in 63.38 and 97.97 seconds. Every independent check
passes, but the complete comparison still fails. The process binding, not
the C++ channel strengths, changes in these segmentation experiments.

Doubling the complement drive again to N80/ng8 and P160/ng16 changes only
the two corresponding C++ channel strengths and their physical binding.
The regenerated geometry passes in 72.05 seconds. All independent electrical
checks pass, but the five-level comparison worsens to 6.670 mV (125.69-second
CTest), so this candidate is not adopted. The stored waveforms show opposite
held-error signs with and without the obsolete inverse-clock posts. Clock
coupling and route placement therefore remain under investigation; additional
drive strength alone does not resolve the discrepancy.

A separate dynamic screen applies a one-volt input transition four
nanoseconds before closing to the 0.20-width upper-clock candidate. Relative
to each circuit's actual input at capture, acquisition errors are at most
9.363 mV, within 20 mV. One transition still has 7.538 mV held-waveform
disagreement, exceeding 5 mV. The screen initially included an overstrict
check that the loaded input equal its ideal source within 1 mV two
nanoseconds after the step; the retained trajectories were rechecked using
the actual loaded capture input, as the ADC contract does. This diagnostic
does not replace or waive the original static unit failure. Further width
reduction needs dynamic acquisition evidence, not just a smaller static peak.

`tests/receive_sampler_extracted.py` preserves the extracted 25 MOS fingers,
nine MIM devices and conserved wiring capacitance. An independent schematic
uses the reviewed MOS dimensions and actual MIM dimensions. Separate supply,
input and clock impedances prevent the two circuits from correcting each
other through the bench. Each starts cold and acquires 1.1, 1.35, 1.6, 1.85 or
2.1 V. After closing the clock, a 250 mV input disturbance tests isolation.
Both outputs must separately meet 20 mV acquisition, held-target and hold
variation limits. The complete output comparison from 30–60 ns must remain
within 5 mV, including the closing transient. The checker also requires the
declared tracking and disturbance stimuli, clock levels and complete 2 ps
observation grid.

This unit bench uses a 100 fF/1 GΩ output load. Dynamic aperture, actual
preamplifier/comparator feedback, thirteen-slice operation, mismatch, corners,
fill and full receiver integration remain separate requirements. Passing
this test alone cannot qualify the ADC bank.

`receive_sampler_integration.py` replaces the sampler at the actual
`differential_receive_conversion` boundary. It matches device ownership,
oriented MOS terminals, dimensions, fingers and all five storage branches,
then removes exactly eleven laws per slice. Parent preamplifiers, DACs,
comparators, supply networks and finite-impedance controller drivers remain.
Private complement nodes must have no external loads. Their explicit zero
initial conditions move into the replacement's cold, zero-charge UIC model.
Ten guard tests exercise actual 1-, 4- and 13-slice exports, including lost
storage, altered geometry, wrong ownership and illicit private-node loading.

`tests/receive_sampler_conversion.py` emits both complete conversion circuits
and runs them with the existing C++RTL feedback controller and unchanged ADC
purpose checks. Each flow must independently pass acquisition, hold, code
accuracy, decision delivery and cadence checks. Their ADC codes must then
agree within one count, with capture targets within 5 mV. This comparison
does not replace the failed unit-level closing-transient check. The current
candidate's complete decks have been emitted; their conversion simulations
are pending. The runner reserves waveform space and serializes full runs
through the shared EDA memory lock. `--emit-only` prepares reviewable decks.

From `analog/`, build the exporter and run the registered CTests:

```sh
cmake --build build/regeneration-audit --target analog_1g_ihp_receive_sampler_export -j1
ctest --test-dir build/regeneration-audit \
    -R '^analog[.]1g[.]ihp_receive[.]sampler_' --output-on-failure
```

The geometry and extracted tests use the shared EDA memory lock by default.
`--concurrent` is reserved for a separately allocated resource budget; tool
address-space caps remain active. Six binding guards and six electrical
checker guards reject altered topology, storage, initial state, missing
stimuli, incomplete trajectories and misleading agreement between two
incorrect outputs.
