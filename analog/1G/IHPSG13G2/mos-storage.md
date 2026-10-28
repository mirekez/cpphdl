# MOS bias storage and measured area

Bias decoupling tolerates voltage-dependent storage; the ADC hold and DAC
accuracy contracts are different. The process experiments explicitly select
MOS gate charge for the transmitter bias nodes while retaining the C++ IC's
nominal 400 pF / 50 pF approximation. They are process realizations of that
storage intention, with distinct equations, rather than identical-equation
Verilog-A exports. Actual charge, gate leakage and finite device response enter
the coupled PSP simulation. The generic C++RTL and generated Verilog-A circuits
remain independently checked against their own nominal equations.

## Current-reference storage

Forty dedicated `sg13_moscap_n` cells, each 30 x 30 micrometres, replace only
the identified 400 pF capacitor. Their substrate terminal connects to ground.
The pinned `moscap_tt` model is characterized at −40, 27 and 125 C. Across
0.85–1.10 V and 100 kHz–1 GHz, minimum storage is 449.80, 443.66 and 436.38 pF
respectively. Leakage is also measured through 1.30 V, reaching at most
42.30 microamps at 125 C. The full transient must stay between −0.1 and 1.3 V;
a nominal storage estimate alone cannot establish this.

The nominal complete transmitter passes its unchanged all-state electrical
checks with this realization: maximum plateau error 9.698 mV against 20 mV.
Its observed bias spans 0.000019–0.966602 V, including cold startup. The cascode
bias still uses its nominal 50 pF capacitor in this first experiment.

The former forty-cell layout occupied 57,565.0514 square micrometres but its
single-via, one-ended gate connections failed extracted AC: one cell retained
only 2.10 pF at 1 GHz against a 10 pF minimum. The revised bank occupies
95,836.4642 square micrometres. Both native gate-contact bars have distributed
via rows. Separate upper-metal meshes carry gate and substrate current;
adjacent substrate collectors merge into wide return columns, and the external
landing sits on a full-width row strap. This is a 70.49% standalone footprint
reduction from the 324,808.8064-square-micrometre 400 pF MIM bank.

Fresh local DRC and adapter-based strict LVS pass, including rejection of cut
gate straps and missing return vias. At TT/27 °C, 0.85–1.10 V and
100 kHz–1 GHz, the extracted forty-cell bank retains at least 435.06 pF. Its
maximum wiring loss relative to the same layout's lumped model is 3.20%,
inside the unchanged 5% budget. Intermediate row/trunk layouts failed and
were retained as negative results. Chip routing, density balancing, field
calibration, extracted PVT and complete transmitter operation remain separate.

The MIM reference also triggers `MIM.gR`: its 266,158.592 square micrometres
of plate area exceed the pinned deck's recommended whole-chip maximum of
174,800. That retained failure motivates changing implementation technology;
no DRC rule is disabled or reclassified as a passing chip result.

## Cascode-bias storage

The second trial replaces 50 pF by 220 parallel HV NMOS units. Each unit has
W = 100 um, L = 0.5 um and ten fingers; drain, source and body are grounded.
TT/SS/FF AC characterization at all three temperatures retains at least
52.331 pF over the originally tested 1.6–2.0 V and 100 kHz–1 GHz. Larger individual units lost useful
high-frequency capacitance and were rejected. The selected repeated small
units preserve the frequency response; their multiplicity is explicit.

The physical bank uses the routed four-terminal MOS macro and connects D/S/B
locally on Metal3. Gates first leave the device on Metal2. Each unit then rises
into separate TopMetal1 gate and TopMetal2 return networks. Eight-micrometre
row straps join 16 µm trunks fed from their centers. All units share the actual
substrate through explicit bulk ports in the verification schematic, retaining
each finite tap. Independent hidden substrate nodes describe a different circuit.

The former lower-metal bank occupied 120,138.456 square micrometres and passed
local DRC/LVS, but its first extracted 1.6 V charge sweep fell from 63.6 pF at
100 kHz to 37.1 pF at 1 GHz, below the 50 pF floor. The new upper-metal bank
occupies 135,316.334 square micrometres. Fresh local DRC and strict transistor
LVS pass with unmodified PDK decks; cut gate straps and removed return vias
fail LVS. Only isolated lower-layer minimum-density violations remain.
The combined two-bank footprint is now 231,152.7982 square micrometres,
versus 371,565.8592 for the two MIM reference banks: 37.79% less.
Fresh complete transmitter tests with both MOS storage realizations pass at
TT/27 C, SS/125 C and FF/−40 C. Maximum all-state plateau errors are 9.410,
13.190 and 11.993 mV, respectively; bias ripple is 6.589, 6.233 and 7.000 mV.
The corresponding shared-line static interaction tests also pass. These tests
retain the original 20 mV error and 15 mV ripple limits.

Startup overshoots the nominal cascode bias: observed maxima are 2.280, 2.270
and 2.291 V. An initial 2.2 V characterization guard correctly failed. The
actual charge/leakage study was extended through 2.4 V at all three corners and
temperatures before fresh transmitter runs were accepted. The earlier failed
receipt is retained. Only exactly zero initial voltage is accepted for the two
replaced storage stamps; nonzero or missing storage is rejected by binder tests.
The upper-metal bank's extracted AC check is separate from those schematic
transmitter tests. It compares distributed RC against the same layout's lumped
capacitance model, with a 5% wiring-loss budget and the unchanged 50 pF floor.
The independent schematic PSP reference must meet the storage floor too;
additional layout capacitance is reported separately from wire loss. Runs use
one simulator process per bias to bound the 2,200-finger bank's memory.
The TT/27 °C extracted check passes at 1.6, 1.8, 2.0 and 2.4 V over
100 kHz–1 GHz: minimum full-bank effective storage is 63.24 pF. This does not
extend the schematic corner results to extracted PVT or a complete transmitter.
This footprint comparison is not accepted die area or full-transmitter proof.

## MOSCAP LVS correction

The pinned dedicated-MOSCAP connection deck links its derived bulk port to
diffusion but omits its link to the Metal1 routing conductor. The unmodified
unit LVS failure is reproduced and retained. `moscap_lvs.py` creates a separate,
audited copy that adds only the two geometric bulk-to-Metal1 connections for
N/P MOSCAPs. It does not modify installed PDK files, device equivalence,
port requirements, tolerances or DRC. Every copied rule and the exact delta
are hashed in the result. Upstream changes require review; the adapter rejects
an already-corrected deck.

The forty-cell bank passes strict LVS with this adapter. A disconnected gate
strap and removed substrate vias are independently rejected. This is explicitly
adapter-based LVS, not a claim that the unmodified upstream LVS passed. HV
MOS gate-storage LVS uses the unmodified deck. Both layout experiments retain
the isolated global-density failures and do not qualify a complete chip.
The pinned Magic documentation lists dedicated N/P MOSCAPs as unsupported
devices. Magic recognizes their channel geometry as an ordinary LV NMOS.
`moscap_extraction.py` therefore requires fresh, independent dedicated-MOSCAP
LVS evidence before binding those channels to the dedicated PDK PSP model.
It checks device count, W/L, all four distributed terminals, return connectivity
and external ports. The dedicated PDK wrapper retains its intrinsic diffusion
geometry convention; ordinary-NMOS extracted diffusion parameters do not
overwrite it. Neither installed PDK files nor their intrinsic equations change.

Raw extracted wiring and charge-conservation records remain available. Exact
Kirchhoff elimination removes unloaded resistor-only junctions to bound memory;
all device terminals and capacitor endpoints remain. An independent nodal
solver checks complete terminal-current matrices on resistor meshes, and
binding guards reject changed geometry, model interfaces and disconnected
returns. This adapter is not foundry or field-solver calibration.

## Replica integration candidate

`mos_gate_storage_adapter.py` exposes the unchanged 220-unit HV storage bank
through physical plate-access routes. Its local DRC and strict LVS pass;
removing the gate route or ground via array is rejected. The gate landing
is left of the bank and the ground landing is right of it, allowing row
routing without crossing the internal gates. This is geometry evidence;
the added adapter still needs extracted charge qualification.

`transmit_translation_bias_mos_layout.py` uses that adapter in a separate
replica candidate. It retains the exported replica transistor, three
resistors and four external ports. The storage bank shares the replica's
physical substrate through every finite guard tap. Eight-micrometre
upper-metal routes connect its plates to BIAS and VSS. The exported 50 pF
storage intention remains a qualification target over the operating range;
the nonlinear MOS charge is not represented as an ideal 50 pF stamp.

`analog.1g.ihp_transmit.translation_bias_mos_layout` checks local geometry,
strict LVS and rejection of a missing storage bank or replica transistor.
Those checks pass for the 197089.255 µm² candidate; isolated minimum-density
findings remain. Conserved extraction retains 2,220 HV MOS fingers and three
R3 resistors. `analog.1g.ihp_transmit.translation_bias_mos_extracted`
reproduces that extraction and runs full 2 µs cold startup at TT/27 °C,
SS/125 °C and FF/−40 °C. It checks the physical and independent schematic
circuits separately before comparing them under the original replica limits.
All three complete startup runs pass, with maximum settled physical/schematic
bias differences of 1.302%, 1.288% and 1.324%, below the unchanged 5% limit.
The maintained emitter produces byte-identical experiments; the existing
waveforms were independently rechecked when composing their evidence.
This is recorded diagnostic evidence, not a claimed CTest execution.
Extracted charge over PVT, all 22 translator loads and the complete transmitter
must also pass before this candidate can replace the MIM default.

`analog.1g.ihp_transmit.translation_bank_mos_storage.{mos_tt,mos_ss,mos_ff}`
registers the next loaded checks. Both complete circuits have been generated
and their actual C++ boundaries validated. The original nominal run now
passes both independent circuits and all 22 comparisons: maximum edge
difference is 89.494 ps and waveform RMS difference is 83.882 mV, below the
unchanged 100 ps/150 mV limits. The junction-model issue below still prevents
qualifying that original circuit.

A subsequent junction-geometry audit identifies 2,200 raw storage-finger
records with `as=0` in that physical deck. These select the PDK's default
junction geometry, overriding their explicit drain parameters. The sampler's
[GDS audit](analog-extraction.md#mos-junction-geometry) demonstrates why that
requires review. Independent polygon accounting now confirms 9,020 µm² of
storage diffusion and 50,204 µm of perimeter across 220 actual GDS instances.
Magic declares half those totals; the PDK's single-finger defaults instead
produce 14,960 µm² and 90,992 µm. This is a model-geometry error, not evidence
of a particular voltage or timing error.

An isolated hierarchical correction binds all 2,220 MOS fingers to their
actual GDS channels and extracted terminal coordinates, repairing 2,200
storage-finger junction records. Six guards pass: diffusion conservation,
preservation of all other circuit records, idempotence, incorrect placement,
missing/duplicate macros and missing fingers. The resistor body partition,
distributed wiring, terminals, transistor widths and lengths remain intact.
The corrected circuit passes the original TT/SS/FF two-microsecond startup
tests, with settled bias differences of 1.302%, 1.288% and 1.324%, below 5%.
Its loaded-bank physical retest is running. The schematic trajectory is
reused only after exact deck equality and independent rechecking of all
original limits; the physical circuit is simulated again with the repaired
junctions. Full-TX retests remain required. Evidence is
under `build/clock-integration/extracted-repair/gate-storage-junction-*`;
this correction is not yet the maintained default. The reusable hierarchy
helper and its seven-guard CTest are described in
[analog extraction](analog-extraction.md#mos-junction-geometry). The original
loaded-bank evidence remains preserved.

## Run

Build the cascoded transmitter fixture and run the relevant CTests:

```bash
ctest --test-dir analog/build -R 'analog.1g.ihp_moscap_bias_storage' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_mos_gate_storage' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_moscap_transmit' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_mos_storage_transmit' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_transmit.translation_bias_mos_' --output-on-failure
```

Each process run starts from the real C++ export and reviewed transistor binder.
Only the two identified cold-start capacitors may be replaced. Their ideal
stamps are removed to avoid counting their charge twice. Raw trajectories,
geometry, source/model/tool hashes and independent bounds remain in results.
The combined trial also records every cascode-bias solver state and checks its
range. MOSCAP has a TT/statistical deck; selecting a MOS transistor SS/FF corner
does not create an unavailable MOSCAP corner. Mismatch, extracted PVT, noise
and full-chip power/voltage sequencing remain to be qualified.
