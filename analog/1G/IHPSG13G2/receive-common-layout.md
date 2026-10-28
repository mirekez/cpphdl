# Shared receive common-mode reference

The selected circuit is [`lead_receive_common_reference.hpp`](lead_receive_common_reference.hpp).
One reference serves the eight receive input networks in the two-port repeater.
Its transistor feedback loop sinks their common-mode current. A resistor divider
sets the target to 243/443 of the achieved supply; the nominal 3.3 V target is
1.81016 V. Output storage and a series RC compensation branch shape its response
to simultaneous changes on the input pairs.

The C++ exporter elaborates that actual IC and records its internal probe nodes.
[`receive_common_layout.py`](receive_common_layout.py) checks all 39 contributions:
11 MOS devices, nine resistors and 19 capacitor contributions. It checks their
connections and values before selecting physical devices. The selected MOS
widths agree with the independent eight-lane PSP binding in
[`lead_receive_common_process.py`](lead_receive_common_process.py).

The layout uses guarded HV transistors with 0.45 µm channels and 5 µm fingers,
RPPD resistors and explicit MIM capacitors. Each 600 µm PMOS is split into two
300 µm units to respect the pinned PCell's finger limit. Parallel capacitances
on identical nodes are combined; large capacitances use parallel MIM banks.
The total nominal intentional storage remains 122.69 pF. The shared substrate
is connected through the physical guard taps, which remain in the LVS schematic.

The initial implementation occupies 566,644.91 µm² before fill. Its maintained
geometry and binding tests pass in 188.68 seconds: 46 matched LVS circuits,
81 normalized devices, three ports, no local geometry/maximum-density violations,
and rejection of a deliberately removed output sink. All 12,117 nonzero extracted
capacitor endpoints are connected. Minimum-density violations remain for chip
fill; this is not a filled macro or an ASIC signoff result.

## Electrical comparison

[`receive_common_extracted.py`](receive_common_extracted.py) replaces exactly
the reference's 39 contributions in the existing eight-lane bench. It retains
all 57 external resistors, 18 external capacitors, finite supplies and stimuli.
It rejects connections from the host to the reference's private nodes.

The schematic flow uses PSP/R3/MIM models with the selected physical dimensions.
The extracted flow adds the conserved distributed wiring RC and the resistor-body
charge model. Only unloaded resistor junctions are eliminated; every device,
capacitor endpoint and observation remains. Private observations bind to actual
extracted conductor identities without shorting distributed branches.

Both flows retain continuous state through 16 µs of settling and 256 ns of
coincident eight-lane switching. Each must independently pass the established
settling, transient, divider-tracking, input common-mode, injected-current and
stimulus checks. The comparison then permits at most 5 mV difference on the
reference output, supply, target and every external input-network observation;
internal control/bias observations have a 50 mV comparison budget. Nominal
passive models are used at the three MOS/temperature corners. `--refined` halves
the warmup maximum step for a convergence check. None of these tests qualifies
the downstream gain stage, ADC, pads or complete PHY.

The nominal schematic passes all twelve independent check groups. Its maximum
settled reference error is 7.09 mV and transient error is 78.00 mV, under
6.81–16.54 mA sink loading. Common-mode and differential RC-equation errors are
at most 2.26 and 3.90 µV. The original extracted layout fails with 35.27 mV
settled reference error, 158.58 mV transient error and 31.90 mV divider error.
The complete electrical comparison has not passed. Eight
maintained integration guards pass, including a private-node load, wrong device
width, aliased compensation node, missing capacitor, changed final-lane waveform
and conflicting source hashes.

From the repository root:

```sh
cmake -S analog -B analog/build/regeneration-audit
cmake --build analog/build/regeneration-audit --target \
  analog_1g_ihp_receive_common_layout_export \
  analog_1g_ihp_lead_receive_common_reference
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog\.1g\.ihp_receive_common_reference\.(layout|layout_guards|extracted_guards|extracted_mos_tt)$' \
  --output-on-failure
```

The build regenerates the exporters. The layout test regenerates GDS, schematic,
DRC/LVS reports and conserved RC under its results directory. Electrical tests
require that fresh fixture and verify its source/artifact hashes before use.
They save compressed complete measurement traces. Generated outputs are not
required as committed source inputs.

## Current-route revisions

Separate overlays in [`receive_common_current_routes.py`](receive_common_current_routes.py)
and [`receive_common_drain_contacts.py`](receive_common_drain_contacts.py) retain
the original device dimensions, storage, placement and conductors. They add a
right-side drain column, a parallel TopMetal1 output bus, a 12 µm TopMetal2
ground return, upper-conductor pins and distributed drain Via2 contacts.

Both revisions pass local DRC, exact 81-device LVS, missing-sink rejection and
conserved extraction. The final revision occupies 573,106.72 µm² before fill,
1.14% more than the original. A distributed unit-current wire-only diagnostic
reduces the output path from 44.59 to 14.65 Ω and the sink return from 15.93
to 4.63 Ω. These are resistance measurements, not loaded transistor acceptance.
The original geometry remains selected; neither revision is electrically
qualified.

Six maintained `current_route_guards` check conductor/device preservation through
a GDS round trip and reject a changed sink binding, missing pin, altered channel
length, removed diffusion stripe and repeated or unprepared overlay. Their CTest
passes in 2.09 seconds. The revised circuit reuses the completed schematic only
after full-deck byte equality and source/waveform hash checks. Its KLU transient
reached 16.256 µs, but export exceeded the 64 MiB reservation. The incomplete
archive cannot qualify the circuit. Complete baseline archives occupy about
118 MiB each; future receiver captures reserve 256 MiB. The retained prefix
already exceeds the reference and divider limits.

A separate DC diagnosis holds all 17 independent sources at their declared
16 µs values and retains every physical device and wiring element. The
schematic output is 1.80714 V; the revised extracted output is 1.84190 V.
The latter reproduces the transient's initial measurement, so extending
settling alone will not repair that error. Internal ground reaches 73.08 mV,
and the feedback input senses 25.27 mV less than the external output.

For diagnosis only, scaling supply, ground and output wire resistances to 1%
reduces the DC reference error to −2.05 mV and divider error to +0.31 mV.
Scaling ground resistance alone exposes the opposing supply drop. These altered
resistances are not a physical implementation or an acceptance result. New
physical routing must pass geometry checks and the original complete transient
checks before selection.

## Separate sense connections

The candidate in [`receive_common_sense_routes.py`](receive_common_sense_routes.py)
separates three sense branches from the loaded buses. The target divider's
upper resistor connects directly to the external VDD pin; its lower resistor
connects directly to VSS. The feedback input connects directly to the external
OUT pin. Each original sense branch is removed before its replacement is added,
so the new wire does not become another load-current return. Transistors,
intentional storage and the remaining current conductors are preserved.

The candidate passes local DRC, exact LVS, missing-sink rejection and conserved
extraction. Its area is 593,507.56 µm² before fill, 3.56% above the preceding
current-route candidate. At the same declared 16 µs DC stimulus, its reference
error is −4.80 mV and divider error −0.78 mV; output differs from the schematic
by 2.39 mV. At the declared 2.1 V and 2.5 V common-mode load extremes,
reference error remains within 5.96 mV, divider error within 0.79 mV and
schematic/output difference within 2.40 mV. These are DC results. The candidate
is not selected by the default layout factory.

The `sense_route_guards` CTest checks conductor/device preservation and rejects
wrong feedback binding, duplicate escapes, wrong pin layers, missing branches
and unsupported/repeated overlays. A failed precondition leaves the layout
unmodified. Its optional `--candidate` argument also checks polygon and metadata
equivalence against independently verified candidate geometry. All seven tests,
including this equivalence check, pass in 4.09 seconds.

The maintained `sense_layout` CTest regenerates this GDS and its local
DRC/LVS, missing-sink and conserved-extraction evidence; it passes in 179.07
seconds. These geometry and voltage checks do not establish current capacity.
Section 2.15 of the pinned SG13G2 Process Specification, revision 1.2, gives
0.4 mA per Via3 cut for 11 years at 105 °C. The sink's drain/source Metal3
collectors have 13/25 Via3 cuts: only 5.2/10 mA total capacity. The actual
high-load DC case supplies 16.47 mA externally, before the internal pull-up
contribution. Both collectors therefore fail even this necessary capacity check.

The queued full transient was retired before simulation started.

## Distributed sink access

[`receive_common_upper_current_routes.py`](receive_common_upper_current_routes.py)
gives each sink diffusion stripe eight Via3 and eight Via4 cuts through
Metal4/Metal5 to upper-metal collectors. Two TopVia1 cuts connect each stripe
to a 2.4 µm TopMetal1 collector. The source collector returns through
TopMetal2. The conflicting old 24-cut central source Via3 array is removed;
its conductors, bottom contacts, devices, storage and separate sense routes
remain. The layout has the same 593,507.56 µm² footprint as the sense candidate.

Local DRC, exact LVS, missing-sink rejection and conserved extraction pass.
Actual connected Metal3 polygons touch 133 drain and 129 source Via3 cuts,
giving necessary aggregate capacities of 53.2 and 51.6 mA. Both declared DC
load extremes pass the original 15 mV reference, 2 mV divider and 5 mV
schematic/physical comparison screens.

A high-load operating-point audit uses the unreduced extracted resistor
network and checks its 39 observed voltages against the reduced model to
within 1 µV. Geometrically matched Via1–4 contacts have a maximum current of
254.96 µA across 4,112 contacts, below the pinned 400 µA limit. Another 127
9 Ω edges cannot be assigned an individual contact by that geometry audit;
their currents are screened separately and peak at 265.28 µA. This is nominal
DC evidence, not complete wire-current, transient, PVT or EM qualification.

The `upper_current_route_guards` CTest verifies device and unrelated-conductor
preservation through GDS serialization, actual collector cut counts, unchanged
footprint, rejection of seven invalid inputs without partial rewiring, and
rejection of repeated application. Its optional `--candidate` argument checks
polygon and metadata equivalence to the independently checked candidate.
All four tests, including that equivalence check, pass in 7.10 seconds.
`upper_current_layout` regenerates the layout and its geometry/extraction
checks from maintained sources; it passes in 106.67 seconds:

```sh
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog\.1g\.ihp_receive_common_reference\.upper_current_(route_guards|layout)$' \
  --output-on-failure
```

The complete 16 µs cold-start and 256 ns switching test has finished for the
isolated eight-cut layout. It fails the original electrical limits: settled
reference error is 19.934 mV against 15 mV, peak error is 147.117 mV against
100 mV, and divider error is 8.291 mV against 2 mV. The input RC-equation and
source checks pass. DC/contact-current success therefore does not establish
switching performance. The first four symbols already reproduce a 146.918 mV
peak and 19.745 mV settled error; internal-node probes retain the original
cold startup to investigate divider rails, feedback and capacitor returns.
The default layout factory remains unchanged pending electrical qualification.

The first post-processing attempt exhausted its 1 GiB address-space limit
while allocating a discarded duplicate of the complete waveform. A streaming
validator checks the same row widths, finite values and strictly increasing
timestamps without retaining that duplicate. The archived full waveform was
rechecked with unchanged electrical contracts and source hashes; the failure
above is an electrical result, not a simulator or storage failure. Four
validator regression cases pass, including matching rejection behavior and
bounded auxiliary memory. The maintained upper-current runner uses this
validation path; it does not alter the shared receiver contracts.

[`tests/receive_upper_current_extracted.py`](tests/receive_upper_current_extracted.py)
provides the reproducible electrical run using the existing host composition
and acceptance functions. It requires the eight-cut candidate's verified
geometry, uses KLU with one solver thread and reserves 256 MiB for each full
trace. Registered `upper_current_extracted_mos_tt`, `_mos_ss` and `_mos_ff`
CTest cases use 27, 125 and −40 °C respectively; registration is not a passing
corner result. Each flow must pass its own checks before comparison.

Emission has been checked from the maintained geometry. All GDS polygons and
the schematic deck match the isolated candidate. The fresh physical extraction
is not byte-identical: it must retain its own provenance and electrical result.
Its nominal DC observations differ by at most 1.02 µV from the earlier
extraction. That diagnostic does not substitute for a full transient.

The maintained [`receive_upper_current_dc.py`](tests/receive_upper_current_dc.py)
now reproduces both load extremes from the emitted decks, including SPICE's
endpoint-hold behavior for the 17 PWL sources. It removes transient initial
conditions, preserves the component laws, and compares complete operating
points. The fresh extraction passes both independent voltage screens and
the 5 mV comparison: physical reference errors are −6.492/−4.494 mV and
divider errors −0.782/−0.776 mV. Reduced/unreduced observations differ by
at most 0.00144 µV. The unreduced current screen passes for 4,112 mapped
contacts and 125 unassigned 9 Ω edges, with maxima 254.961/265.281 µA.
The resistor mesh differs from the earlier extraction, so its unassigned
edge count is recorded separately.

`upper_current_dc_guards` passes six tests covering stimulus interpolation,
endpoint hold, changed source/observation boundaries, incomplete or ambiguous
raw output, and independent reference/divider failures. The maintained
`upper_current_dc_screen` CTest requires `upper_current_emission`, which in
turn requires `upper_current_layout`; selecting the screen regenerates both
fixtures. The DC screen normally waits for the shared EDA memory slot.
`--concurrent` is only for a separately budgeted 1 GiB slot. The completed
screen used that slot with already verified geometry/emission receipts.
After the streaming-validator change, fresh emission and the isolated DC
CTest pass in 2.04 and 28.14 seconds respectively. Contact attribution also
verifies the coordinate file against the original extraction's artifact hash.
These checks do not qualify transient currents, all wires, PVT or full EM.

The nonloading four-symbol probe run now reproduces the original waveform
prefix exactly. Two output-storage plates differ from OUT by as much as
77.821 mV; their ground returns span approximately −27 to +46 mV. The target
divider's capacitor returns sit 36.205–47.350 mV above external ground.
Feedback-gate errors stay below 2.376 mV. A separate resistor-network audit,
with Kirchhoff residual checks, finds about 85 Ω from OUT to the probed plates
and 47 Ω on their returns. These measurements identify storage access as a
physical repair target; the probe run is not a full qualification result.

[`receive_common_storage_routes.py`](receive_common_storage_routes.py) adds
direct TopMetal1 access to the output bank and distributed TopMetal2 ground
returns for that bank and the target-divider capacitors. Bottom-plate vias
are placed outside the MIM top plates. All devices, intentional capacitances,
original wires and separate sense connections remain. The isolated layout
passes local DRC, exact LVS, missing-sink rejection and conserved extraction
in 115.58 seconds, without enlarging its footprint. Fresh emission and the
original DC/contact screen pass in 2.34 and 29.14 seconds.

Four maintained routing regressions pass in 7.60 seconds, including exact
polygon/metadata equivalence to that independently checked candidate and
rejection of invalid banks or pins before any geometry is changed.
`storage_layout` regenerates the candidate through the established geometry
checker and passes in 117.59 seconds; `storage_route_guards` uses the
distributed-current layout fixture. The regenerated extraction retains its
own provenance and still needs its own electrical result.
The first four-symbol electrical diagnostic retains the original 16 µs cold
startup, source schedule and 2 ps measurement grid. It is running; complete
256 ns switching, corners, fill and chip-level qualification remain required.
The default factory remains unchanged.
