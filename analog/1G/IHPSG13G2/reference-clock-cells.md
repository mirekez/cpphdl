# Compact clock-cell realizations

The existing hand-routed PLL fails extracted frequency acquisition. These
modules evaluate compact process implementations of the detector and divider
purposes using actual IHP standard cells. They have not qualified a complete
replacement physical PLL.

`clock_cell_library.py` reads the PDK transistor circuits without modifying
parameters. It checks pins, device counts, model families, body connections
and geometry-parameter inventories. `reference_clock_cells.py` declares the
phase detector; `reference_clock_divider_cells.py` declares the divider.
These are functional process implementations, not identical-equation exports
or transistor-by-transistor bindings of the generic C++ latch. The generic
C++ and Verilog-A implementations retain their own solver comparisons.

The detector converts each rising input edge into an active-low pulse using
five inverters and a NAND gate. Parallel NAND cells capture that pulse in a
static latch. A separate pulse driver feeds the common reset logic, keeping
that route's capacitance off the local capture node. An AOI cell combines
both captured states, external clear and pulse-qualified reset feedback.
Reset stays asserted until the input pulses end, preventing recapture during
clear release. The complementary outputs are the reset-dominant stored
states; inverters drive the true outputs. Parallel cells are actual connected
devices in both schematic and layout.

The current 864 square micrometre layout passes local DRC, strict LVS with
106 normalized devices, a cut-reference negative, and conserved RC extraction.
Three targeted schematic and extracted cases pass the unchanged absolute
contract: TT/27 °C wrapped phase with no output load, and SS/125 °C coincident
and wrapped phase with 20 fF loading. The full matrix and schematic/physical
comparison are running. These targeted results do not establish full PVT,
full-loop acquisition or chip qualification. All 42 schematic matrix cases
and all 42 extracted typical/slow-hot/fast-cold cases and comparisons now
pass. All seven CTests pass, including the geometry fixture.

Earlier native-flip-flop storage passed all 42 schematic cases but missed a
reference edge during extracted common-reset recovery. Pulsed alternatives
with OAI storage also failed extracted edge or reset timing. A NAND revision
passed edge/reset states but delayed its complementary output in a second
buffer: at 207.75 ns UP was 0.020 V and UP_N was 0.521 V with a 1.193 V supply.
The current direct stored-complement connection removes that redundant stage.
All failed diagnostic receipts remain in the build directory; limits did not
change to admit the new candidate.

The maintained detector matrix covers seven edge/frequency profiles, unloaded
and at 20 fF, with TT/27 °C, SS/125 °C and FF/−40 °C at fixed 1.2 V. Each case
calls the contract shared with generic C++ and Verilog-A tests: edge ordering,
frequency-error direction, asynchronous clear, complementary outputs and
finite-supply operation. Each flow must pass independently before comparison.
Physical/schematic limits are 20 mV plateau RMS and 10 mV mean signed-error
difference. Voltage corners remain an additional qualification requirement.

```sh
cmake --build analog/build/regeneration-audit --target prepare-ihp-extraction-precise
cmake -S analog -B analog/build/regeneration-audit
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog\.1g\.ihp_reference_clock_cells\.' --output-on-failure
```

`CPPHDL_IHP_CLOCK_CELL_MAGIC_EXECUTABLE` selects the precise-value extractor
for these LV clock macros. It is separate from the established sidewall tool
used by other active physical tests. Preparation followed by configuration
registers the geometry fixtures when the tool is available. SPICE cases use
one thread each so CTest controls concurrency explicitly.

The divider feeds its inverted stored output back to D, with separate clear
and true-output inverters. Its schematic and compact 224 square micrometre
layout previously passed six corner/load cases at 250 MHz, including a
100 fF/10 kΩ load and asynchronous clear. Local DRC, strict LVS with 38
normalized devices, cut-clock rejection and conserved RC passed. The slow/hot
maximum edge difference was 90.06 ps within 100 ps. Shared router/tool changes
require fresh geometry and provenance checks before integration.

```sh
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog\.1g\.ihp_reference_clock_divider_cells\.' --output-on-failure
```

`clock_cell_layout.py` routes native PDK GDS using reviewed LEF pin access.
Disjoint horizontal routes share tracks; DRC and LVS independently verify
spacing and connectivity. Minimum-density violations remain explicit for
chip fill. Named extraction labels actual anonymous conductors, then verifies
unchanged devices and the complete capacitance matrix. A separately audited
correction removes coupling duplicated onto nodes whose original ground
capacitance was zero. This does not establish field-solver calibration.

The C++ exporter records complete detector/divider ownership and ports in
`realizations.json`. `reference_clock_realization.py` checks disjoint ownership
and rejects external connections to private nodes before binding a process
netlist. Outer-owned storage is retained before capacitor aggregation. Five
malformed-partition guards and the real exported partition pass.

The current NAND detector, native divider and C++ circuit's ideal outer R/C
pass the complete 2 µs schematic diagnostic: OUT 250.001495 MHz, divided clock
124.999999 MHz, control ripple 0.324 mV and maximum translated delay 768.45 ps.
All shared PLL limits and input-identity checks pass over 544,021 samples.
This supersedes the earlier native-flip-flop diagnostic. The first unfilled
extracted PLL fails cycle variation (124 ps versus 80 ps), translated delivery
(1.53 ns versus 1 ns), and tuning ripple (51 mV versus 2 mV). Its mean frequency,
divided phase wander, rails and supplies pass. Its queued fill was stopped.
The next placement brings tuning and mirror-bias storage into the oscillator
row; geometry verification and a separate wiring diagnostic are active.
Supply corners, noise, calibrated extraction, filled routing and complete-chip
verification remain required.

The separate full-PLL placement optimizer now uses stable net ordering and
accurate cost summation. Fourteen placement/hierarchy guards pass, including
fresh processes with three Python hash seeds. Deterministic regeneration does
not renew the failed electrical qualification of the previous physical PLL.

`clock_macro_layout.py` embeds each native clock IC through its Metal3 pins
and reserves separate Metal4 access columns. Local row integration passes
DRC, LVS, deliberate clock-disconnection rejection and conserved extraction
for both macros. `reference_clock_realized_layout.py` composes them with the
42 outer nonstorage devices and 51 original storage branches. The oscillator
feedback stays within one physical row; support devices occupy another.

The composed candidate occupies 1351.7 × 383.56 µm. Its unfilled geometry
passes strict LVS with 259 normalized devices and local DRC; nine minimum
density categories still require fill. The native-cell circuit models tie
NMOS bodies to VSS. Custom transistor and resistor substrate terminals must
therefore use the same physical wafer net. The row builder's explicit
`substrate_net='VSS'` binding retains the custom tap devices. Its default
remains a separate internal `PSUB` for layouts without that native binding.

`analog.1g.ihp_reference_clock.realized_layout` regenerates the C++ export,
builds the composition, fills it and requires full macro DRC, exact LVS
inventory and rejection of a disconnected reference-clock via. The optional
`--unfilled` script argument is only a local diagnostic; its receipt cannot
claim full DRC. Passing local geometry does not establish the filled result,
extracted loop behavior or a complete ASIC.

`analog.1g.ihp_reference_clock.realized_extracted` consumes that filled proof,
regenerates the same C++ circuit, extracts the actual filled GDS, and applies
the existing PLL electrical contract to the conserved RC network. It rejects
unfilled layouts, stale input hashes, a different GDS, or a failed disconnected
reference test. `realized_evidence` exercises those rejection paths before any
long simulation. Complete transient samples stream to a lossless archive.

The composed PLL needs the double-precision geometry serializer built by
`prepare-ihp-extraction-double`; select its executable through
`CPPHDL_IHP_PLL_MAGIC_EXECUTABLE`. The filled test is registered but has no
passing electrical result yet. Its first fixture was stopped after the
unfilled candidate failed electrical checks. It will resume with a corrected
candidate; an unfilled result cannot substitute for its filled proof.

The detector proof was refreshed after maintaining the resistor-body extraction
anchors. All four selected CTests pass: local layout and 42 extracted cases
across TT, SS and FF, each checked independently and against its unchanged
schematic trace. The refresh took 7,446.58 s. This renews the detector component
proof; the composed PLL still requires its separate extracted electrical and
filled-layout checks.

The divider refresh also passes all four selected CTests (382.50 s): local
layout plus both output loads at TT, SS and FF using the current extraction
helpers. Its unchanged schematic proofs are reused explicitly by the fixture
selection; all extracted waveforms and comparisons are fresh.

The local-storage placement completes its 2 µs PEX run but still fails:
7.255 mV control ripple, 90.507 ps output cycle variation and 1.435–1.473 ns
delivery. The oscillator's LOW node itself has 15.01 ps cycle variation; the
output path adds substantial variation and delay. A separate real power-route
prototype passes local DRC and exact LVS without increasing area. Adding
parallel first-source Via1 and source Via2 access initially exposed same-net
Metal2 spacing gaps at five body landings. Explicitly joining those gaps passes
fresh local DRC, exact 259-device LVS and open-reference rejection. Source-to-
ground wire resistance at the four tuning NMOS devices falls from 33–36 Ω to
18–21 Ω. Both completed power-routing transients fail delivery and control ripple,
as detailed below. Filled qualification remains open; these DC resistance
diagnostics alone do not establish clock acceptance.

A third isolated prototype adds direct connections to both plates of the actual
3 pF control-storage bank, retaining its dimensions and total capacitance. It
passes local DRC, exact 259-device LVS and the open-reference negative test.
The extracted capacitor ground-return resistance falls from 46.17 Ω to 7.93 Ω.
Its completed 2 µs transient passes cycle variation at 51.51 ps, but delivery
remains 1.067–1.086 ns and tuning ripple is 31.17 mV. Both fail their unchanged
limits. The source-ground ripple is 28.73–29.59 mV and correlates about 0.98
with the tuning voltage; the improved capacitor return alone is insufficient.

An additional isolated candidate distributes Via1/Via2 cuts along source and
drain stripes on 18 wider transistors. Separate lower/upper contact regions
retain legal spacing at the LV finger pitch. Local DRC, exact 259-device LVS
and open-reference rejection pass with unchanged transistor dimensions and
footprint. The extracted transient is running after the source-contact candidate;
no timing improvement is claimed before that run completes.

The power-route-only transient completes with cycle variation 76.73 ps (passes),
output frequency 249.991 MHz and divided frequency 124.998 MHz. Delivery improves
to 1.138–1.162 ns but still exceeds 1 ns; control ripple is 32.75 mV and also
fails. The complete waveform and unchanged-input proof are retained. The
direct control-storage-return candidate also completed, with the failure above.

A further isolated routing candidate adds 12 µm TopMetal2 ground straps over
both folded rows, with distributed physical TopVia2 cuts. This targets the
27–29 mV tuning-source ground motion observed in the completed power-routing
waveform (correlation 0.971–0.979 with tuning voltage). The candidate passes
local DRC, exact 259-device LVS and the deliberately disconnected reference
check, at the unchanged 472,691.6728 µm² footprint. Its extracted network is retained; the queued electrical check was superseded
before starting by the candidate with upper supply pins. This is a physical wiring experiment, not a passing clock
or a change to the electrical acceptance limits.

The source-contact-only candidate completed its full 2 µs transient. It passes
cycle variation at 52.10 ps, but delivery is 1.067–1.086 ns and tuning ripple
is 31.69 mV; both still fail. A further supply-access candidate retains all
metal and exposes VSS on its TopMetal2 strap and VDD/VLOGIC on TopMetal1.
Local DRC, exact LVS and disconnected-reference rejection pass. Six pin guards
pass in CTest, including rectangle/polygon inputs, GDS round trips, and absent
or incomplete supporting conductors. The extracted tuning-capacitor ground
return falls to 1.623 Ω. This candidate's queued electrical check was superseded by the buffer relocation; the
intermediate strap-only transient was superseded before starting, with its
complete extraction retained. Full-chip routing must connect the upper pins.

The first buffer relocation allowed a fresh oscillator-wide ordering. Although
local DRC/LVS passed after joining same-net via-pad shoulders, it increased
tuning-wire coupling from 105.33 to 187.46 fF. A limited relocation instead
preserves the original oscillator order and moves only the larger buffer and
three storage components beside the predriver. It passes local DRC, exact
259-device LVS and disconnected-reference rejection in 444,292.8904 µm²,
about 6% less area. Six CTest guards check ordering, complete component
preservation, malformed boundaries and row overflow. Predriver wire load
falls from 106.06 to 51.66 fF; tuning-node wire load remains 105.33 fF.
The stronger buffer's wire load grows from 76.07 to 130.99 fF. Full transient
validation is running; the prior supply-only transient was superseded before
starting, with its extracted network preserved. A separate slow-process,
125 °C nominal-supply schematic screen is also running.
