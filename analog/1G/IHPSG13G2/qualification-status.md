# Working circuit and area priorities

The two-port repeater is unfinished. Electrical correctness has priority; area
reductions are accepted only after the connected process circuit passes the
same limits. Preset trim values characterize a circuit and do not implement
calibration. Generic C++RTL/Verilog-A agreement tests the exported equations;
IHP PSP tests additionally exercise intrinsic transistor charge and body effects.

The composed native-cell PLL passes schematic checks but its first unfilled
PEX run fails cycle variation, output delivery and tuning ripple. Frequency,
divided phase wander, rails and supplies pass. The candidate's queued fill
was stopped. A revised placement keeps tuning and mirror-bias storage with
the oscillator. Its complete extracted transient reduces ripple to 7.255 mV,
but still fails the 2 mV ripple, 80 ps cycle-variation (90.507 ps measured) and
1 ns delivery limits (1.435–1.473 ns measured). An archived-network diagnostic
separates wire-resistance effects from capacitive coupling. No complete clock-macro result is claimed. A separate power-routing prototype
passes local DRC, exact 259-device LVS and open-reference rejection without a
footprint increase. Its extracted tuning-source return resistances fall from
62–65 Ω to 33–36 Ω. Its complete transient passes cycle variation at 76.73 ps,
but delivery (1.138–1.162 ns) and control ripple (32.75 mV) still fail.
The source-contact candidate further reduces cycle variation to 52.10 ps,
but delivery (1.067–1.086 ns) and control ripple (31.69 mV) still fail.
The direct capacitor-return candidate still fails delivery (1.067–1.086 ns)
and control ripple (31.17 mV), while cycle variation passes at 51.51 ps.
The distributed-contact candidate passes cycle variation at 49.62 ps, but
delivery (1.016–1.034 ns) and control ripple (31.90 mV) still fail.
The upper-metal supply-pin candidate places both buffers in one row, passes local geometry and six
placement guards, and occupies 6% less area. Its complete 2 µs extracted run
passes delivery (0.972–0.976 ns) and cycle variation (29.24 ps), but fails
frequency (249.872 MHz), divided phase wander (209.22 ps) and control ripple
(7.99 mV). Acquisition oscillation contributes to the final 400 ns measurement;
the last 100 ns still has about 4.22 mV control ripple against the unchanged
2 mV limit. Fill remains stopped pending electrical repair.
An unchanged-circuit probe run reproduces those results and finds only
0.634 mV ripple on the tuning capacitor's ground terminal, versus 65.09 mV
on the main filter output and 64.88 mV on its return. A real-R3 sizing trial
with 3 kΩ smoothing and 4 kΩ damping passes frequency (249.999 MHz),
phase wander (16.81 ps), cycle variation and delivery, but fails tuning
ripple (4.64 mV). Its final 100 ns still has 3.70 mV ripple. A second
trial uses 8 kΩ smoothing and 6 kΩ damping. Both retain the previous
external wiring RC and require matching C++ export and fresh physical
layout/extraction before qualification.
The 8 kΩ/6 kΩ retained-wire diagnostic also fails: divided phase wander is
244.481 ps and tuning ripple is 11.191 mV. Frequency, cycle variation and
delivery pass. The separate fresh C++/layout/extracted candidate also fails
its complete 2 µs run: 293.471 ps divided phase wander and 12.078 mV tuning
ripple. Frequency, cycle variation (65.565 ps) and delivery (0.969–0.978 ns)
pass. Its conditional fill sequence stopped on that failure. A
3 kΩ/8 kΩ diagnostic also fails: phase wander is 83.36 ps and tuning ripple
8.92 mV; the other checks pass. A second diagnostic uses 1.5 kΩ smoothing,
6 pF control storage and 4 kΩ damping, preserving approximately the 9 ns
smoothing time constant of the 3 kΩ/3 pF trial while reducing control-node
impedance. It fails frequency, phase wander (243.82 ps) and tuning ripple
(11.73 mV). Both use PDK-sized devices with the previous external RC;
neither qualifies a GDS layout.
Slow/hot schematic screening passes all eight electrical groups at nominal supplies: 250.001 MHz output, 0.341 mV tuning ripple and 0.882 ns maximum delivery. Fast/cold schematic screening also passes all eight groups over the complete 2 µs: 250.002 MHz output, 0.362 mV tuning ripple and 0.692 ns maximum delivery, with unchanged inputs. These results do not
establish a passing clock macro or filled layout.

The shared TX translation replica now has a C++-derived physical implementation
with its actual 50 pF storage. Local DRC/LVS, missing-device rejection, complete
startup and independent/comparison checks pass at TT/27 °C, SS/125 °C and
FF/−40 °C. The macro occupies 75,638.83 µm², contains 18 LVS devices including
the explicit taps, and reaches 1.768–1.855 V with at most 1.33% settled error.
The wider-metal variant below passes the nominal loaded bank; fill,
passive/supply corners and full TX qualification remain open.

The latest separate TX translator passes both SS/125 °C loads in the local
extracted diagnostic: 92.98 ps at 0.2 pF and 97.68 ps at 2 pF, below the
unchanged 100 ps comparison budget. Short upper connections for the inverter
output and pull-down nodes create the improvement. Both independent waveforms
also pass their absolute limits. All six maintained guard, geometry and TT/SS/FF tests pass (874.28 s).
A separate TT startup diagnostic with one translator and the actual replica
also passes: 94.32 ps maximum edge difference and 86.96 mV waveform RMS.
The first maintained connected matrix passes TT/FF but fails SS at 106.48 ps.
Direct physical connections to the replica storage then pass the SS diagnostic
at 96.53 ps/74.54 mV RMS without increasing area. Both maintained replica tests
pass; all three maintained connected corners and their guards also pass
(1,278.57 s), with maximum edge differences 83.52/96.53/75.13 ps at TT/SS/FF. The C++-derived 22-translator bank
has passing topology/observation guards. Its complete nominal schematic startup
and simultaneous-switching stress pass for all 22 translators, with shared bias
1.773–1.854 V. All 22 extracted gates pass their absolute limits, but the
121.71 ps edge difference exceeds the unchanged 100 ps comparison limit.
The extracted shared bias spans 1.545–1.924 V. A wider-metal candidate with
upper-layer BIAS/VSS access passes local DRC, strict LVS, missing-device
rejection and three unloaded startup corners. Its wire-only storage access
falls from 7.66–7.98 Ω to 0.22–0.54 Ω on BIAS and from 3.12–4.27 Ω to
0.95–2.10 Ω on VSS. The complete 2.128 µs nominal loaded-bank test now passes
all 22 independent gate checks and the comparison: worst edge difference
88.964 ps and waveform RMS 83.013 mV, under the unchanged 100 ps/150 mV
limits. Shared bias remains within 1.723–1.832 V. Supply/passive corners
and complete TX remain open.

The first connected full-TX schematic simulation stops at 39.357 ns with
an R3 pullup convergence error, before controller switching begins. Its
incomplete output is a failed run. The maintained 64 ns startup regression now
passes with 1 pA and 0.1 pA absolute current tolerances. All 77 voltage
observations differ by at most 0.464 mV, below the 1 mV numerical budget.
Twelve guards pass. With the loaded replica bank now passing, both complete
transmitter circuits have been emitted and the schematic run is queued for
the shared memory slot. Full-window electrical acceptance and the extracted
connected flow remain required.

The shared [receive reference layout](receive-common-layout.md) passes its
maintained local DRC/LVS, missing-sink rejection and conserved-RC checks
(566,644.91 µm² before fill). Nine topology/binding guards and eight integration
guards pass. Its eight-lane nominal schematic passes all twelve independent
check groups, with 7.09 mV settled reference error and 78.00 mV transient error
under 6.81–16.54 mA sink loading. The baseline extracted layout fails with
35.27 mV settled error, 158.58 mV transient error and 31.90 mV divider error.
The revised current-route simulation reached the end of its transient, but
waveform export exceeded its 64 MiB reservation. Its incomplete archive cannot
qualify the circuit. The saved prefix already shows 55.77 mV settled reference
error and 23.82 mV divider error, above the unchanged 15 mV and 2 mV limits.
Circuit diagnosis is required before another full run. Future receiver captures
reserve 256 MiB: complete baseline archives occupy about 118 MiB each. The
slower duplicate SPARSE run was intentionally retired; its exit 143 is not an
electrical verdict. A subsequent sense-routing candidate passes local DRC,
exact LVS, missing-sink rejection and conserved extraction. Separate divider
and feedback connections reduce its DC reference error to 4.80 mV, divider
error to 0.78 mV and schematic/output difference to 2.39 mV. Seven regression
checks pass, including geometry equivalence to that candidate. Its maintained
geometry-regeneration CTest also passes. A subsequent pinned-PDK current check
finds only 5.2/10 mA drain/source Via3 capacity against at least 16.47 mA required.
The unstarted transient was retired. Distributed upper-metal sink access now
passes local DRC/LVS at the same 593,507.56 µm² footprint. Eight Via3/Via4
cuts per stripe give 53.2/51.6 mA aggregate drain/source Via3 capacity.
Both DC load extremes pass the original voltage/comparison screens. The
unreduced high-load DC audit finds a maximum 254.96 µA among 4,112 mapped
Via1–4 contacts, below 400 µA; the 127 unmapped 9 Ω edges also remain below
that limit. These checks do not qualify wire density, transient currents or
PVT reliability. Four maintained routing tests pass, including geometry
equivalence and invalid-input rejection. The complete original receiver
transient now fails: 19.934 mV settled reference error, 147.117 mV peak error
and 8.291 mV divider error, against unchanged 15/100/2 mV limits. Input
RC-equation and source checks pass. Internal-node probes are investigating
the switching failure; no receive-macro pass is claimed. Post-processing
initially exhausted memory by duplicating the trace. Streaming validation
recovered the complete archived waveform without another simulation or any
change to electrical limits; four validator regression cases pass.
The maintained DC/contact-current screen also passes on the fresh regenerated
extraction (fresh 28.14-second CTest after the validation change), including both load extremes, unreduced-model
agreement and coordinate-file provenance. Six stimulus/output guards pass.
The fresh resistor mesh has 125 unassigned 9 Ω edges, all below 400 µA;
its contact maximum remains 254.961 µA. This remains nominal DC evidence.
Completed nonloading probes exactly reproduce the failed waveform prefix and
show up to 77.821 mV between output storage plates and OUT, with target-capacitor
returns 36.205–47.350 mV above ground. An optional direct storage-access overlay
now passes local DRC/LVS/extraction, the same DC/contact screen and four routing
guards. Its four-symbol diagnostic is running with the original cold startup;
neither that short diagnostic nor DC success qualifies the full receiver.

The [physical receive sampler](receive-sampler-layout.md) has passing local
DRC, strict 21-device LVS and missing-hold/dummy rejection. Its fourth layout
connects the downstream load at the hold plate and occupies 15,243.42 µm².
Both schematic and extracted circuits pass independent acquisition/hold
checks at five input levels, but closing-transient comparison still fails:
28.042 mV versus the unchanged 5 mV budget. Ten new integration guards pass
against actual 1-, 4- and 13-slice exports. Complete ADC comparison decks
retain the preamplifier, DAC, comparator and C++RTL controller; their
simulations remain pending. No qualified sampler or ADC macro is claimed.
An optional GDS-derived junction correction now restores omitted dummy-device
diffusion area; six guards pass for both 25- and 34-finger layouts. Corrected
five-level simulations still fail comparison at 28.018 mV for the baseline
and 19.681 mV for the stronger-driver/source-contact candidate. Raw extraction
and the queued parent inputs remain preserved; generic extraction is unchanged.
Isolated C++ sizing and upper-clock-routing trials reduce the best complete
five-level mismatch to 5.519 mV at 15,005.95 µm². Every independent
acquisition/hold check passes, but the original 5 mV comparison still fails.
A separate one-volt late-step diagnostic finds at most 9.363 mV acquisition
error relative to the actual loaded input; one transition still differs by
7.538 mV between schematic and layout. Smaller complement drivers and removal
of all redundant inverse-clock posts do not improve the worst static result.
These candidates have not replaced the baseline or qualified the ADC.

The large receiver hit the pinned router’s known optional clock-NDR restart
loop. Its sized checkpoint is preserved. A fresh [routing recovery](receiver-routing.md)
uses ordinary PDK spacing and repeats all downstream acceptance checks. The
first recovery was killed by the kernel OOM handler during another simulation's
waveform export; it produced no routing verdict. A fresh recovery is queued
from the same verified post-sizing checkpoint. It has no routed pass yet.

The 64 µs extracted processing-clock simulation reached its stop time, but
waveform export exhausted disk space. The compressed archive ended at
61.247 µs and failed its gzip integrity check; it cannot establish a complete
result. A fresh run verifies byte-identical circuit and controller inputs,
reserves 1.75 GiB of actual disk blocks for compressed output, and holds the
shared EDA memory slot through simulation, export and checking. Its test
window, numerical settings and acceptance limits are unchanged. The complete
extracted result remains pending.

The [three-domain reset-controller macro](phy-reset-physical.md) now passes
native/RTL/mapped and routed replay, filled DRC and strict LVS in 140 × 140 µm.
Nominal extracted setup/hold slack is +2.176/+0.270 ns. Downstream reset
distribution, recovery/removal, MTBF and final fill coupling remain open.

The [transmitter MOS banks](transmit-mos-layout.md) pass the six main/history
steering, sink and cascode geometry cases. They use parallel via cuts into
center-fed TopMetal1 collectors; the 1,500 µm cascode has three guarded banks.
Strict LVS also rejects a deliberately missing bank. Six-case extraction
preserves every finger and the intended external connection points. Nominal TT
DC checks pass with at most 3.54% current error against independent equal-width
PSP references. Unfilled metal minimum density, connected driver layout,
transient/PVT electrical behavior and EM remain open.

The [50 Ω line pullup](transmit-pullup.md) now passes local geometry, strict
LVS, missing-section rejection and extracted TT DC testing. Eight parallel
sections use distributed contacts and center-fed upper-metal collectors.
Their individual body couplings are matched by physical identity, with charge
conservation and negative parser tests. Full-chip density, thermal/EM and
connected transmitter qualification remain open.

The [connected four-device current cells](transmit-current-cell.md) pass local
DRC/LVS, cut-input rejection and conserved extraction. Both steering directions
pass extracted TT DC comparisons at 1.3/2.3/3.3 V line bias and half/nominal/
four-thirds current. Worst errors are 4.00% main and 3.08% history, below the
unchanged 5% local budget. Wider current routes, sixteen-cut TopVia2 landings
and direct branch ports corrected an initial 16.0% layout loss. Complete
transmitter composition and PVT/full-chip qualification remain open. Six
extracted 42 ns switching cases also pass at an 8 ns symbol interval, using
ideal gate and bias sources. Each waveform passes independent differential
and total-current bounds before comparison; worst differences are 3.61% main
and 2.90% history. Actual translators, bias startup and line loading remain open.

The separate [PSP charge-ownership candidate](mim-area-budget.md) retains two
explicit local capacitors per current cell and assigns eight generic charge
terms to the corresponding process MOS terminals. Five ownership guards pass.
Its main/history storage macros pass maintained local DRC/LVS, missing-capacitor
and substrate-tap rejection, and conserved extraction (115.82 s). All six
42 ns extracted current-cell/storage switching cases pass (327.38 s), with
maximum differences of 3.61% main and 2.93% history, using the original
independent and comparison limits. This remains a candidate: full transmitter
loading and chip integration have not adopted it. The current eight-transmitter
component inventory exceeds recommended whole-chip MIM area even before
receiver and clock storage are included.
The complete candidate's 64 ns numerical startup now passes at both current
tolerances, with maximum departure 0.504 mV across all 77 voltage outputs
against a 1 mV budget. Both full transmitter circuits are prepared and queued
for the shared memory slot; this startup result does not establish switched
transmitter operation.

The [connected trim/cascode bias macro](transmit-bias.md) passes local DRC,
strict LVS, missing-switch rejection and all 64 extracted TT trim codes.
Four-cut routing vias and wider buses repair the first candidate's loss and
nonmonotonic carry transitions. Physical/schematic checks pass independently;
maximum comparison error is 3.299% against the unchanged 5% local budget.
The footprint is 221,281.85 µm². A fresh maintained bias/guard run passes
(119.13 s) after the shared row helper changed. Full TX storage/loading/startup,
calibration, PVT, mismatch, fill and EM remain open.

The new [physical transmitter partition](transmit-physical-partition.md) exports
all 931 actual C++ contributions exactly once across 33 blocks. Eight partition
and six current-cell binding guards pass. The actual eight main/history cells
match the qualified physical banks; retained storage has explicit access to
their existing TAIL/LOWER RC anchors. Complete loaded and routed assembly is
still required.

The latest PAUSE datapath repair completed detailed routing with zero router
violations and exact 630,084-cycle replay. Extracted setup is +0.091719523 ns,
but one SRAM-address net still exceeds the 80 fF limit. Its two antenna diodes
made the point-to-point repair incorrectly classify it as multiple functional
loads. The helper now buffers its sole logic sink while preserving the diodes;
the real-cell connectivity CTest passes. Fresh detailed routing finishes with
zero router violations, exact 630,084-cycle replay, setup +0.089458220 ns,
hold +0.112074018 ns and no electrical violations. The final crossing-check
supervisor times out while waiting for the shared memory slot. A fresh
supervisor verifies the retained route and replay, then queues crossing and
geometry checks with their execution budgets starting after admission.
The previous interrupted and failed receipts remain available.

The repeater now uses [Ethernet PAUSE flow control](../flow_control.md), as
selected by the user. The forwarding path has two 4096-word synchronous frame
buffers mapped to eight actual IHP dual-port SRAM macros. Independent C++, RTL,
PDK SRAM and IHP standard-cell replays pass 630084 observations, 240 intact data
frames, six PAUSE/six resume frames, and unsupported-peer/reset isolation.
The dedicated FIFO test also passes stopped-transmit prefill and bank/pointer
wrap; it catches the repaired false underrun before the first SRAM read.
The actual FLP composition negotiates symmetric PAUSE in both C++ and RTL over
264750017 reference cycles. `NegotiatedRepeater` now connects these negotiation
results directly to forwarding, with immediate disable on PMA loss/restart.
Its maintained C++/RTL integration CTest passes with agreement over 264761099
observations, including received PAUSE and stopped-clock link loss.
The first pipelined SRAM/control route completed with zero router violations
and exact functional replay, but failed extracted setup at −0.516693354 ns
and exceeded signal-slew limits. Hold was +0.004137135 ns. A new registered
read-credit stage separates Gray decoding/pointer subtraction from queue and
SRAM-issue control. It uses only the second synchronizer stage. Native FIFO/PAUSE, FIFO/forwarding/negotiated RTL, PDK SRAM and mapped-cell
tests all pass. The renewed placement passes setup +0.302903503 ns and hold +0.080113918 ns;
a 2.5-times wire-capacitance optimization trial then failed global setup at
−1.948678613 ns after inserting long buffer chains. It was stopped before
detailed routing. Restoring nominal PDK RC and allowing setup buffer removal
passes global setup +0.311036527 ns and hold +0.117311329 ns; clock capacitance
and six fanout violations remained in that estimate. Detailed routing completed
with zero router violations and the exact 630084-cycle replay. Extracted setup
(+0.188799202 ns) and hold (+0.011086354 ns) pass, but 710 slew, 576 capacitance
and six fanout report entries fail. A further FIFO change separates read issue
from same-edge queue consumption while retaining full frame throughput. Its
native, RTL FIFO/forwarding/negotiated, PDK SRAM and mapped-cell checks pass.
The renewed global route passes setup +0.593975544 ns and hold +0.115303881 ns,
with no slew or fanout violations, but fourteen clock branches exceed the
80 fF capacitance limit. A targeted branch split now handles both buffer leaves
and directly attached register clock pins. All fourteen branches pass a real
checkpoint connectivity exercise. The fresh global route now passes setup
+0.604821086 ns and hold +0.120023437 ns, with no electrical or antenna
violations. Detailed routing finished with zero router violations and exact
630084-cycle replay. Extracted setup (+0.444233119 ns) and hold
(+0.088663414 ns) pass, but 36 slew and 180 capacitance report entries fail.
The capacitance failures comprise 142 signal nets, 23 clock nets and 15 input
ports. The next candidate uses three-times wire capacitance during signal
repair and 0.26 ns / 60 fF finishing targets, then restores the original
0.30 ns / 80 fF acceptance limits. A concurrent converter rebuild required
fresh SRAM/cell proofs; both pass with a pinned converter, and fresh placement
passes setup +0.294023484 ns and hold +0.080415234 ns. The new global
route passes setup +0.332676113 ns and hold +0.120145671 ns, with zero
electrical and antenna violations. Detailed routing and exact replay completed.
Extracted setup is +0.132938102 ns and hold +0.113469906 ns; only two
capacitance entries remain (one signal and one clock), with no slew/fanout
violations. The clock branch includes an inverter load that the repair helper
previously rejected. The helper now preserves inverter leaves, and a second
helper splits overloaded point-to-point signal wires. The real OpenROAD
connectivity regression passes. At most two extracted-load repair passes are
allowed, each followed by fresh detailed routing and extraction; the original
limits and final replay remain mandatory. Fresh SRAM and mapped-cell CTests
pass after a shared C++HDL header changed. Fresh placement, route and
independent geometry/crossing qualification are queued. The fresh global route again passes setup
+0.332676113 ns and hold +0.120145671 ns with zero electrical/antenna violations.
Detailed routing is running. The
[physical flow](flow-control-physical.md) checks extracted crossing delays
separately from synchronous timing, then streams actual SRAM GDS for full DRC/LVS.
These remain macro results; complete MDI latency, slave transmit-clock recovery
and full-chip qualification are not established.

The physical processing oscillator's compact resistor layout passes conserved
extraction and the six fixed-trim cases in the controller's acquisition interval.
The preceding early-settling stress failed at 119.844 ps against 80 ps and remains
recorded. A newly tested Magic tile-area correction prevents negative area
allocations for breakpoints outside their tile; 3,244 independent partition
cases pass. Fresh oscillator, gate and reference-PLL extractions conserve
capacitance. Field-solver calibration remains open.

A separate analytical oracle now reproduces the pinned extractor's sidewall
coefficient defect: all 70 nonzero checks return approximately half the
expected coupling. An isolated Magic candidate removes the duplicate parser
halving. It passes all 84 checks across seven metals and both parser keywords,
including orientation, scaling and shielding; single/series MIM extraction
also passes. The hierarchical PLL extraction used this candidate. Existing processing
clock simulations keep their original tool inputs. These coefficient checks
do not establish field-model accuracy or upgrade older extracted results.

The ADC reference PLL's compact oscillator placement passes complete filled
DRC and strict LVS, including rejection of a disconnected input. The physical
inventory contains 526 normalized devices. Its full 2 us extracted electrical
simulation failed: the output settles near 124.689 MHz and the divided output
near 62.298 MHz, instead of 250/125 MHz. The reference-side detector also
misses alternate reference edges. Its failed waveform is retained. Geometry
inspection found detector enable/reset connections spanning folded row cuts.
The placement objective now counts both row buses and the connecting spine;
six targeted placement tests pass in CTest. The new candidate reduces the
reference-master enable span from 1610 to 237 um and the feedback clock-inverter
span from 1597 to 103 um. Other paths grew. The new layout passes full DRC with zero violations and strict
LVS with all 526 normalized devices; its bounding area is 2.067 mm2. Fresh
extracted electrical simulation failed: OUT is 166.612 MHz and DIVIDED
83.288 MHz in the final 400 ns. The reference detector still misses edges;
control ripple is 59.5 mV against the unchanged 2 mV limit. The next placement
uses exported C++ ownership to retain each complete master/slave flip-flop
inside one physical row. Twelve hierarchy/placement checks pass; new full
DRC/LVS and electrical qualification are required. Independent pre-fill checks
confirm unchanged physical devices/topology and all 91 components of each
flip-flop retained within a row. The new footprint is 1.611 by 1.404 mm.
Full filled DRC passes and strict LVS matches all 526 normalized devices.
The corrected-sidewall extracted simulation still fails: OUT is 125.031 MHz,
DIVIDED 62.448 MHz, and the reference detector responds to only 25 of the
50 reference edges in the final 400 ns. Control ripple is 142.7 mV. The
common-reset wire contributes 785 fF of extracted interconnect load; the
reference master-Q wire adds 223 fF. A startup diagnostic retains the same
network and observes internal detector nodes to locate the missed-edge cause.
The clock remains unqualified.

A full 2 µs sizing trial reduced the common-reset R3 resistor from 1 kΩ to
200 Ω while retaining the old extracted wiring. It still failed: OUT was
124.991 MHz, DIVIDED 62.552 MHz, and control ripple 137.4 mV in the final
400 ns. That resistor-only change was not adopted. Increasing the four
reset-NAND channels fivefold also fails the complete 2 µs test: OUT averages
167.012 MHz, DIVIDED 83.494 MHz, and control ripple is 139.9 mV. The detector
produces only 34 UP pulses for 50 reference edges in the final 400 ns. Its
retained waveform shows reset recovery and latch propagation overlapping the
next reference edge. This sizing trial has no matching GDS and was not adopted.
A compact PDK standard-cell realization of the same sequential functions is
being evaluated separately; it does not qualify the existing PLL layout.
The maintained cell-divider schematic tests pass all six load/corner
combinations at 250 MHz. Fresh generic detector/storage C++ and Verilog-A
regressions and their comparisons pass. Earlier detector output buffers fail
slow/hot loading or extracted reset timing. A new gated-output detector makes
reset decisions from stored state independently of load and suppresses both
controls while storage clears. It passes all 42 schematic corner/load cases. The extracted wrapped-phase
case still misses the reference edge at 160.05 ns during common-reset
recovery. The resulting reversed phase polarity blocks adoption. Its
675.84 µm² symmetric layout passes local DRC, strict LVS
(96 normalized devices), cut-clock rejection and conserved RC; the first
routing short was detected and corrected before those checks passed.

The divider's first 264.32 µm² layout passes the absolute electrical oracle
at slow/hot but exceeds the 100 ps schematic-comparison bound at 110.07 ps.
Moving the clear driver and removing unnecessary port stubs reduces it to
224 µm². All six extracted corner/load cases then pass independently and
against their schematic, with slow/hot maximum difference 90.06 ps. All seven divider CTests passed after the common parser was separated.
Later shared-router edits require refreshed geometry/provenance checks.
Isolated minimum-density violations remain for later chip fill. These are
component results, not a repaired or qualified full PLL. See
[compact clock-cell realization](reference-clock-cells.md).

A new whole-IC ownership binding removes the detector/divider implementation
by C++ device identity while retaining surrounding connections and storage.
Its first full-loop **schematic diagnostic** passes the existing 2 µs contract:
250.001554 MHz output, 125.000001 MHz divided feedback, 1.464 mV control ripple
and 768.15 ps maximum output delivery. The generic outer R/C values remain
ideal; this does not qualify the failed physical detector or a new PLL GDS.

The processing-clock controller had a second acquisition defect: after its
512-reference-cycle monitor detected drift, it used the 128-cycle threshold to
choose the retuning direction. The corrected controller passes 698,640 matching
native/RTL/mapped-cell events, including slow and fast drift recovery without
reset. The previous controller fails the new directed regression. Fresh routed
replay also passes, with extracted setup +0.018183399 ns and hold +0.061225358 ns.
The two extracted Gray buses pass their independent delay/skew checks. Full
DRC passes with zero violations; strict LVS matches 20,540 devices and 40 ports. The new native and Verilog-A 64 µs closed-loop delivery runs independently pass
at 134.132 and 134.467 MHz. Their direct comparison passes: frequency differs
by 0.249% (1% limit), delivered-waveform normalized RMS by 20.342 mV (60 mV
limit), and sustained readiness by 128 reference cycles (417-cycle limit).
The PSP transient completed all 64 us and its electrical checks pass at
133.773 MHz, with 3959 delivered pulses and 175.7 ps maximum delivery delay.
Its overall receipt fails provenance because `cpphdl_array.h` changed during
the run. The exact former header was recovered and hash-checked from Git for
historical reproducibility. A fresh source build and export now reproduce the
controller library, Verilog-A model, metadata and complete PSP circuit byte
for byte. A separate artifact-equivalence receipt therefore links the current
source to the completed electrical checks; the original failed receipt is
preserved. This is not a new transient, native-solver or chip proof. The older
extracted delivery run timed out after 12 hours at approximately 37 us of the
required 64 us. Its partial controller event log shows admission but cannot
qualify delivery. A fresh extraction with the corrected sidewall tool is being
prepared. The full-run wall-clock budget is now 24 hours, with the same circuit,
64 us duration, timestep and electrical acceptance bounds; CTest also allows
time for the serialized full-trace checker. The small startup checker does not
need to wait behind memory-intensive routing.
The first Verilog-A run exhausted its
1 GiB allocation limit while creating output vectors; the identical complete
circuit was resimulated successfully with 3 GiB reserved. The original failed
receipt remains unqualified; older 64 us successes below are
historical evidence, not qualification of the corrected controller.

The integrated negotiated PAUSE repeater again passes its C++/RTL test. The five
changed RTL hierarchies have been regenerated from fresh passing proofs and
linted. The receiver resume stopped before routing on the changed shared
header; fresh native/RTL checks pass, and mapped checks are running. Receiver routing, physical
slave transmit-clock generation, connected
TX/RX analog macros, full PHY/PCS assembly, padframe/power and full-chip
verification remain unfinished. No complete repeater GDSII is qualified.

The detailed component history below includes superseded candidates and their
original measurements; it does not replace the current status above.

## Receiver

The recorded precharged baseline is `charge_balanced_receive_frontend<16>`.
The current cold-start candidate is described below. The baseline retains 1.5 pF
hold storage, a 0.25 pF compensation step and a 30-micrometre PMOS sampling
dummy. It removes eighty provisional 1-gigaohm returns and retains floating
latch-node charge with thirty-two explicit 10 fF stack capacitors. Basic
classes preserve their previous defaults; this receiver explicitly selects
charge-defined nodes. The process binder checks the resulting topology.

The complete fixed-control bank passes C++RTL and Verilog-A at both gain
settings and their independent comparison. The 2.5 ns acquisition stress
passes gain 4 at 18.329 mV but fails gain 8 at 21.968 mV against the unchanged
20 mV limit. This failure remains a characterization result. Inspection of the
actual SAR sequencer shows two tracking clocks: 8 ns at 250 MHz. The closed
conversion tests, which use that real controller, determine operating acceptance.
A 1.75 pF sampling-storage trial worsened the short-window error to 23.259 mV;
it is rejected. Increasing dummy width helps one gain setting and harms the
other, so that change is not selected either.

The full sixteen-slice, two-burst gain-4 closed test passes C++RTL, Verilog-A,
IHP PSP and comparison. Maximum code errors are 1, 1 and 2 counts; process
acquisition and hold errors are 11.272 and 14.572 mV. Gain-8 also passes all
three flows and generic comparison: maximum code errors are 2, 1 and 2 counts;
PSP acquisition and hold errors are 7.176 and 14.887 mV. These are recorded
precharged-bank baselines, before the subsequent cold-start source change. Receipts are under `tests/results/charge_balanced_conversion_16_gain*`
in the build tree. No isolated or short initialized diagnostic qualifies the
complete bank.

The passive MDI input network passes independent DC/transient oracles in
C++RTL and Verilog-A, including all 625 two-driver level combinations, common
mode transitions and timestep refinement. Its common mode is about 1.91 V.
Connecting that mean directly to the existing single-ended PGA shifts its
output bias; integration therefore remains open. The [differential gain stage](differential_receive_gain.md) now passes the full
nominal process matrix, both generic flows and their comparison. NMOS tap
selectors avoid the PMOS-only version's low-common-mode failure. A 500-ohm
follower return reduces DC current and improves headroom. The slow/hot 1.65 V
unit stress still fails linearity; coupled process sweeps must establish the
actual operating range before that boundary can be accepted.

The [shared physical reference](receive_reference.md) passes generic flows,
comparison and all three declared process corners under the equivalent static
load of eight channels. The new `differential_receive_preamplifier` connects
that reference, the passive MDI network, both outputs of one input amplifier,
two buffers and the PGA. Both generic flows and comparison pass; its complete
50-MOS process circuit passes TT/27 C, SS/125 C and FF/−40 C. At a 2.3 V
line common mode, achieved PGA input means range from 1.238 to 1.274 V across
those conditions.
These checks still use finite linear line-source impedances and nominal
passives; no physical TX/ADC/echo-cancellation integration is claimed.

The preceding receiver's sixteen-slice generic closed test passed with one
count maximum error in each simulator. Its process fixed-control stress test
failed at 21.645 mV against 20 mV. Increasing the output follower to 1200 or
1600 micrometres still failed the short acquisition diagnostic, so those larger
followers were rejected. Its full closed PSP run lost controller-event evidence
and remains failed, even though a subsequent electrical diagnostic found a
maximum two-count code error. Missing evidence is never reconstructed into a
passing receipt. Controller trace writes now fail immediately on storage errors.

## Transmitter

The earlier 296-MOS programmable driver passed isolated process transients,
but failed when two drivers shared the line. Its maximum per-driver static
error at nominal TT was 42.278 mV against 20 mV. A correct summed line voltage
can hide opposite errors from the two output stages, so the new regression
checks each stage's terminal currents separately.

The candidate [cascoded transmitter](cascoded-transmitter.md) has 304 MOS devices.
Its extra cascodes preserve current when the remote end changes the achieved
line voltage. The nominal process transient passes: maximum error 11.308 mV,
bias ripple 7.523 mV, and active supply current 42.04–43.55 mA. Its four generic
Verilog-A cases pass, including 80/100/120-ohm loads and timestep refinement;
maximum error is 2.818 mV. All three declared process corners now pass: TT/27 C, SS/125 C and FF/−40 C.
SS maximum error is 8.158 mV. The four C++RTL cases and direct generic waveform comparison also pass;
maximum comparison difference is 11.295 microvolts.
Receipts live under `build/1G/tests/results/cascoded_core_transmit_driver/` and
`build/1G/IHPSG13G2/tests/results/cascoded_core_transmit_*`.

The maintained two-driver DC test exports the actual C++ circuit, checks its
strict process binding, then connects both output stages to one differential
line and a shared 0.25-ohm supply impedance. All 625 current/history combinations
are tested. All three corners pass: TT 11.171 mV, SS/125 C 11.635 mV and FF/−40 C
10.501 mV maximum per-driver error. The original stage is rejected by a
required negative regression. This is
a static output-stage test with ideal gate controls and nominal passives, not
a replacement for the complete translator/controller transient tests.

## Area and physical boundary

The selected transmitter needs 400 pF bias storage and 50 pF cascode-bias
storage. The 100 pF variant failed generic ripple; the 200 pF variant passed
generic tests but failed PSP at 22.497 mV error and 15.771 mV ripple. Neither
smaller variant is accepted. The compact 400 pF MIM bank exceeds the PDK whole-chip recommended MIM
area and remains a recorded negative chip-budget case. Actual MOS storage
provides a smaller alternative. The revised upper-metal cascode-storage bank
and dedicated MOSCAP bank together occupy 231,152.7982 square micrometres,
37.79% below the two-bank MIM reference footprint. The old lower-metal bank
failed its first extracted high-frequency check at 37.1 pF against the 50 pF
floor. Fresh upper-metal geometry/LVS and both disconnection negatives pass;
extracted TT/27 °C AC also passes over 1.6–2.4 V and 100 kHz–1 GHz, retaining
at least 63.24 pF. The dedicated MOSCAP bank now uses distributed gate vias
and merged upper-metal return collectors. Fresh local DRC, adapter-based LVS,
both disconnection negatives and TT/27 °C extracted AC pass: minimum 435.06 pF,
maximum wiring loss 3.20% over 0.85–1.10 V and 100 kHz–1 GHz. Its former
single-via unit failed at 2.10 pF instead of the required 10 pF. Dedicated-model
binding requires independent LVS and retains all four distributed terminals;
raw wiring and exact passive-node reduction records remain available.
Extracted PVT, field calibration and full-transmitter composition remain open.
The complete schematic
process transmitter with both replacements passes TT/27 C, SS/125 C and
FF/−40 C: maximum errors 9.410, 13.190 and 11.993 mV. Shared-line static tests
also pass at all three conditions. Preset trim codes remain characterization,
not automatic calibration. [MOS storage and its explicit LVS correction](mos-storage.md)
document charge/leakage characterization, local geometry, the dedicated-MOSCAP
LVS adapter, retained global-density failures and integration limits.
Footprints remain distinct from routed macro or accepted die area.

The standalone receive filter's recorded GDS is 1.21 square millimetres;
mapped cell area decreased by 45.84 percent. Its DRC/LVS evidence applies to
the recorded source version. After the shared runtime-header merge, fresh alignment and receive-filter
native/RTL/IHP-mapped functional comparisons pass. Those functional runs do not
renew physical provenance automatically. Post-fill coupling extraction remains
outstanding.

Local MOS, MIM and polysilicon-resistor generators provide building blocks,
not an extracted AFE. Tiny coupling capacitors, resistor PVT/current limits,
common mode and termination at the actual line interface, physical clock
recovery, complete PHY integration, pads/ESD integration, full-chip DRC/LVS and
extracted performance remain open. There is no complete-chip GDS qualification.

## ADC slice-count study

The [parameterized SAR bank controller](../adc_interleaved_bank.md) passes native
and converted-RTL checks with exact complete trajectories for 12, 13 and 16
slices. Thirteen and sixteen each deliver all 4,096 independently checked
samples at one result per controller clock. The twelve-slice negative case
reports premature reuse. A thirteen-slice analog bank would reduce slice count
by 18.75%; the physical receiver remains at sixteen until closed electrical and
routed timing checks establish that saving. No count ratio is reported as GDS
area.


## Cold differential receiver

The new [cold-start differential receiver](differential_receive_frontend.md)
connects the passive input network, both polarities of one amplifier, the PGA,
a physical ADC-range reference and real charge-balanced SAR slices. Every
explicitly stored capacitor voltage starts at zero and both supply rails ramp
up. The thirteen-slice controller has a separate native/RTL/IHP mapped proof.
The default analog class remains at sixteen slices during qualification.

The precharge-only revision precharges real storage through its tracking switches
while the rails rise. Its twenty-micrometre cancellation dummy replaces the
thirty-micrometre cold-start trial. The latter failed the single-slice gain-8
PSP acquisition bound at 23.215 mV; a retained continuous diagnostic reproduced
that failure before testing smaller dimensions. That revision's one-slice gain-8
native and PSP runs pass. The thirteen-slice gain-4 native and Verilog-A runs
pass with zero maximum code error. The complete gain-4 PSP run passes with 12.836 mV acquisition error,
14.018 mV hold variation and two-count maximum code error. These runs still
use the earlier 100 ps host edge. The complete gain-8 PSP run fails: acquisition
23.817 mV, hold variation 24.347 mV and three-count maximum code error.
Historical source snapshots and original waveforms are retained.

Its gain-8 native test fails acquisition at 25.352 mV. Investigation
found an inconsistent control edge: the live SPICE bridge uses 100 ps while
the experiment exports 2 fs and native switching is instantaneous. Matching
the exported edge in a separate copied Verilog-A deck exposes 23.256 mV
acquisition error. Refining its grid to 2 ps gives 23.149 mV, confirming that
the grid difference does not explain the failure. The earlier 100 ps passing
waveform cannot qualify equivalent stimulus. The maintained live bridge now uses and validates the exported edge; its
regression checks both 2 fs and 100 ps declarations and rejects invalid values.
Earlier passing receipts do not qualify the revised bridge. Neither copied
experiment changes the maintained circuit or constitutes a physical
control-driver proof.

The contract now checks the captured input and every held conversion state
against the ADC input window, and all PGA states against achieved supply
rails. Between-aperture conversion-window excursions are retained as
characterization. Seven negative/boundary tests enforce that distinction,
the final-decision hold interval, cold precharge and the original 20 mV
sampling/hold and two-count code limits. The analog default remains sixteen
slices until the complete candidate is qualified.

The preceding low-kickback revision combines thirty-percent stronger sampling switches
with smaller conversion preamplifiers. It keeps 1.5 pF hold storage and both
dummies unchanged. The two preamp input transistors are halved in width;
load, degeneration and tail resistances are doubled, reducing intrinsic
input charge and current while preserving approximate DC gain. A matched
one-slice 2 fs process diagnostic passes at 16.976 mV acquisition, 10.531 mV
hold variation and one-count code error. A 1 pF storage-only diagnostic fails
hold at 22.580 mV and is rejected. The native full-bank 1 pF trial was stopped
when this process rejection made it unnecessary; no native verdict is claimed.
The declared one-slice circuit now passes fresh native, Verilog-A and PSP
flows; generic codes agree exactly. PSP acquisition/hold errors are
16.976/10.531 mV, with one-count maximum code error. Complete thirteen-slice
Verilog-A tests pass both gain settings. The restarted complete native tests
also pass: gain 4 has 1.994 mV acquisition error, 3.024 mV hold variation and
zero-count code error; gain 8 has 19.607 mV acquisition error, 6.070 mV hold
variation and two-count code error. All 26 codes agree exactly with Verilog-A
at each gain. Gain 8's native acquisition margin is only 0.393 mV against
the unchanged 20 mV bound. Both complete PSP reruns finished. Gain 4 passes:
12.805 mV acquisition error, 11.835 mV hold variation and two-count maximum
code error. Gain 8 fails acquisition and conversion accuracy: 26.329 mV
acquisition error and three-count code error; hold variation is 17.431 mV.

The current sampler candidate uses main NMOS/PMOS widths 37.5/75 µm and
dummy widths 20/42 µm, with explicit finger counts 5/10/2/5. Hold storage
remains 1.5 pF and the smaller conversion preamplifier is unchanged. All five
isolated input phases pass the original limits: worst acquisition 17.479 mV,
hold variation 15.964 mV and code error two counts. Both dummy polarities
matter; improving only the earlier worst phase left another phase failing.
The complete thirteen-slice, two-burst tests now pass at gains 4 and 8 in
C++RTL, Verilog-A and IHP PSP under `balanced_sampler_13_gain{4,8}`. Each
flow independently passes the original 20 mV acquisition/hold and two-count
conversion bounds. PSP acquisition/hold maxima are 8.700/13.562 mV at gain 4
and 17.775/19.477 mV at gain 8. Both generic comparisons pass the unchanged
one-count tolerance: gain 4 agrees exactly; six gain-8 codes differ by one
count. The source, artifact and complete-waveform hashes were independently
audited, and the acceptance limits were recomputed.

This qualifies the declared nominal fixed-control bank fixture. The gain-8
hold margin is only 0.523 mV; real clock controls, process corners, mismatch
and extracted loading remain separate requirements. The default analog slice
count remains sixteen. Source and geometry audits preserve the earlier failed
revision. The [circuit explanation](differential_receive_frontend.md) describes
the measured DAC-to-held coupling and exact selected geometry.

The four full-bank native/PSP runners were interrupted before producing
verdicts. Their orphan simulator processes could no longer reach a live
waveform collector. Those run directories are retained with `interrupted`
status; they are neither electrical failures nor passing evidence. Fresh
reruns used the same frozen circuit and checks and have now completed as
reported above. The completed Verilog-A receipts retain valid source hashes
and were reused for comparison.

The ADC-range reference uses a physical 1275/1000-ohm divider, targeting
1000/2275 of achieved Vdd (about 1.45055 V at 3.3 V). Under an eight-channel
equivalent 112.5-ohm load stepped through 0.9..1.5 V, it passes both generic
flows, their 5 mV comparison and TT/27 C, SS/125 C and FF/−40 C PSP. Settled
process errors are 8.445, 8.289 and 7.648 mV against the original 15 mV limit.
Its broader 1.1..1.9 V load characterization failed at 16.252 mV and remains
recorded. The connected midrail-reference input-range sweep passes all four
nominal cases (line means 2.1/2.5 V, gain codes 4/8); PGA terminals stay within
the stated 0.9..1.5 V reference load envelope. The corresponding nominal/slow/fast connected input sweeps also pass.
These unit conditions do not qualify an eight-lane assembled reference.

The complete thirteen-slice controller plus calibration and tags passes 8,320
native/RTL clocks with every observed output equal: continuous and gapped
requests, changing calibration, clipping, reset/restart, precharge and disable
flush. Mapped and fanout-repaired IHP gates pass the same independent tests.
Before placement, the revised precharge-capable controller has slow-corner
cell-only setup slack +0.51 ns at 4 ns, including 0.16 pF analog-control
loads, 0.4 ns input/output delays and 0.1 ns uncertainty. Its repaired LEF
cell footprint is 76,917.8592 µm². These figures do not establish routed
full-lane timing or die area.

The [composed receive lane](../adc_receive_lane.md) adds real digital startup
sequencing: 4096 precharge clocks, four opening clocks, then conversion.
Native, converted RTL and IHP-mapped gates pass 15,468 clocks and 2,914
independently checked samples. Two 500 µm routed candidates failed setup
by 0.06 ns on `valid_out` and 0.09 ns on `acquire_out`. The current composition
removes sequentially redundant ready gating from those outputs; its full
trace remains unchanged, and 60,000 additional native clocks compare reset
and enable boundaries against the original nested bank. Fresh native/RTL/
mapped checks pass. That direct composition's route then failed setup by
0.04 ns inside the normalizer, with +0.04 ns hold slack. Its partial-product
adder now uses carry selection; 409,477 independent unit clocks and the
unchanged full lane trace pass all digital flows. That route passed exact
gate replay but failed setup by 0.173278 ns on the rounding/clipping path;
hold passed at +0.053446 ns. Direct carry/sign decoding now shortens that
path without changing latency. The unit and full lane pass fresh native,
RTL and mapped checks, and the native boundary test passes. The new route
passes setup at +0.077566847 ns and hold at +0.053977489 ns, with exact routed
gate replay. Full DRC passes 868 categories with zero violations. Strict LVS
on the same filled geometry passes 238 ports, 45 circuits and 67,292 devices
when the three proven disconnected compatibility pins are preserved through
simplification. The fresh complete physical run including this verification
adapter now passes extraction, exact replay, fill, DRC and LVS with the same
timing margins. Its 500 × 500 µm filled digital module occupies 0.25 mm².
Measured comparator-return hold buffers
prevent claiming connected analog timing under the current uniform I/O
constraints. Actual analog startup integration and post-fill coupling
extraction remain open.

The standalone raw thirteen-slice controller has a recorded 400 × 400 µm
layout with exact native/post-route trajectories, +1.24/+0.08 ns extracted
setup/hold slack, strict transistor LVS and zero violations across 868 DRC
categories. This excludes calibration, startup, analog circuitry and chip
integration. The corrected 360 × 360 µm controller now also passes the complete flow:
exact 8,256-clock trajectory, +1.10/+0.06 ns extracted setup/hold slack,
zero violations across all 868 DRC categories, and strict LVS (39 matched
circuits, 163 top ports and 34,832 instantiated devices). Its 0.1296 mm²
layout is 19% smaller than the 400 µm layout. Metal2/3/4 global densities
are 36.45/36.99/37.49%. The initial failed result remains retained. The
accepted correction adds exactly 0.01 µm² of Metal3 at one routing corner,
uses legal fill, and restores array bit identity after DEF import through
a separately tested simulation adapter. This remains a standalone raw
controller result; full-lane and chip qualification are open. See the
[physical flow](adc_bank_physical.md) for reproduction and evidence boundaries.


## Shared input common-mode candidate

The [lead-compensated reference](RECEIVE_COMMON_REFERENCE.md),
`lead_receive_common_reference`, passes fresh native, Verilog-A, and IHP PSP
TT/27 °C, SS/125 °C and FF/−40 °C checks with eight finite-source input
networks. Its physical 10 kΩ/12.15 kΩ divider declares VDD × 243/443
(1.810158 V nominal). The 150 µm HV sink uses a 1000 Ω/2 pF compensation
branch and **96 pF total output storage** (80 pF internal plus 16 pF external).

All original limits remain: 15 mV settled, 100 mV transient, 2 mV divider
tracking, and 1.80–2.00 V input common mode. Worst PSP results are 11.957 mV
settled, 89.368 mV transient, and 1.804613–1.996397 V input range across
those corners. Every-state source-profile, exact passive RC, rail, drive,
excitation and 6–20 mA loading checks pass. Native/Verilog-A maximum waveform
difference is 0.574 mV against the unchanged 5 mV bound.

Receipts are under `build/1G/IHPSG13G2/tests/results/lead_receive_common_centered96/`,
with corner receipts in the sibling `_ss_hot/psp/` and `_ff_cold/psp/` directories.
`qualification_audit.json` verifies all five source-hash sets and byte-identical
physical decks against passing retained diagnostics. `official_target_audit/checks.json`
connects those receipts to the normal CMake bench: stimulus, conditions and
mapped deck match byte-for-byte; generic contributions match after source-path
comment normalization. The normal generators updated only the two lead-reference
models. All four CTest contract, source-profile, explicit-target and physical-binding
guards pass; the binding test rejects twelve deliberate mutations.

Earlier 6/11-target failures remain preserved, including the original shunt
regulator and the nominal-only 200 µm/84 pF candidate. Their source snapshots
and diagnostic receipts are retained. The centered96 alternative improves
SS input margin from 0.276 mV for original-target108 pF to 3.603 mV while
saving 12 pF.

This is **not yet integrated into the receiver**. The bench contains passive
100 Ω/150 fF input loads, not eight active amplifier/PGA/ADC chains. It covers
MOS process/temperature corners with nominal R/C, not resistor/capacitor
variation, mismatch, extracted distribution, pads/ESD, or complete PHY behavior.
The established passive input network remains the selected baseline.


## Core-to-DAC interface candidate

The [translated receive DAC](core_receive_dac.md) connects eight 1.2 V trial
controls to actual level shifters and a 3.3 V R/2R ladder. Complete refined
sixteen-word runs pass C++RTL, Verilog-A and IHP TT/SS/FF, including the
expanded input-PMOS exposure check. Maximum aperture errors are 0.492464,
0.491783, 0.953715, 1.889146 and 0.672082 counts, respectively, against two
counts. Worst checked core-device exposure is 1.437201 V against 1.65 V.

The comparison now checks both simulators' observed grids and passes at
3.192400 mV against 5 mV. It independently rechecks the absolute results
and source/waveform hashes. A regression rejects a spike visible only on
the SPICE grid. The earlier 119.577 mV switching discrepancy remains
recorded; timestep refinement resolved it without changing the circuit,
excitation or limits. Native switching uses 50 fs steps and SPICE's declared
measurement maximum is 0.5 ps. This qualifies the declared DAC unit fixture;
the receiver still needs the connected control and comparator paths and the
[physical half-cycle budget](receive_timing_budget.md).

The new [connected decision circuit](core_receive_decision.md) adds real
evaluation/clear translators, the preamplifier, comparator, decision latch
and core return. Its generic Verilog-A unit, current TT/27 °C and FF/−40 °C
process units pass all sixteen decisions. TT and FF have 1.369 and 1.002
counts maximum DAC error, respectively.
The current SS/125 C process run fails fifteen return windows and reaches
2.149 counts of DAC error against the two-count bound. A separate TT
diagnostic with delayed controls, finite slew and 35 fF return loading fails
eight return windows. Stronger latch buffers and reduced preamplifier storage
are isolated sizing experiments, not qualified replacements. Contract and
physical mapping guard tests pass, but there is no connected timing
qualification yet. The second-order native solver experiment passes the full generic
native/Verilog-A comparison with the original 5 mV bound. The maintained
decision test now selects that integration method and refined time grids;
the physical slow-corner circuit remains unqualified for this path.

The reduced-storage sizing experiment makes all sixteen final decisions
correct, but seven remain late: its latest stable return is 3.819 ns after
reducing return-node capacitance, against the 3.6 ns window start. DAC error
remains 2.476 counts. A new input-clear latch removes large reset clamps from
the retained outputs; its settled logic and topology tests pass, but its
complete slow/hot result still has seven late decisions (latest 3.821 ns) and
2.475 counts of DAC error. Its wrapper also flags a shared digital test-runner
edit; the executed dependency helper is unchanged, and the original failure
receipt is retained. A separate artifact audit reproduces both candidates
byte for byte. The asymmetric DAC candidate passes the threshold limit at
1.911558 counts, but still misses seven return windows (latest 3.766500 ns).
Reducing explicit evaluation-translator storage to 50 fF and retained latch
storage to 20 fF improves the latest return to 3.721029 ns and DAC error to
1.881594 counts. All sixteen final decisions are correct, but seven still
miss the unchanged 3.6 ns window start. The complete 640,179-state SS/125 C
run finished with intact source and waveform checks. Intrinsic PSP charge
remains present. These candidates do not replace the maintained circuit.

The experimental second-order native integrator passes exact RC/RL/LC,
nonlinear MOS discharge and coupled-capacitor solutions, plus event,
continuation and rejected-step regressions. A refined first-word comparison
passes at 1.766 mV. The complete sixteen-word native run now passes all
absolute checks. The refined Verilog-A simulation completed, but its wrapper
timed out while verifying the 1.1 GB compressed trace. The complete gzip stream
and simulator row count are retained. A separate bounded-memory audit
verifies the complete archive and original dependencies; its absolute checks
and full comparison pass. Maximum difference is 1.814582 mV on both grids,
within 5 mV. The original wrapper failure remains recorded. The maintained
solver now exposes [explicit BDF2 selection](../../doc/integration.md), while
backward Euler remains its default. The maintained decision fixture uses
125 fs BDF2 steps after a refined final warmup and a 100 fs maximum generic
SPICE step. Trace capture now waits for complete archive verification, and
row validation avoids duplicating millions of states. The historical full
comparison receipt belongs to the recorded experiment; it is not relabeled
as a fresh run of the maintained fixture.

The [shared sampling-control prototype](shared_receive_track.md) fixes
premature/repeated selection events in its second generic version. Its full
32-event edge audit passes, but the physical gate path closes about 0.84 ns
after the declared aperture and reaches 78.313 mV sampling error. Its process
run also fails: all thirteen physical closing edges occur, but the closing
delay is 1.322828–1.334000 ns and maximum held-target error is 111.214 mV.
EVENT also remains above 20% of VDD into its required idle window. The
complete process trace passes startup and exposure checks but cannot qualify
the original 20 mV sampling bound. A common sampling delay may be absorbed by data-driven receive timing,
but that requires a hardware resampler fed only by fixed-rate ADC samples.
The new resampler and tagged symbol-valid interface pass 504,533 exact
native/RTL clock states across twelve fixed-sample phase/rate cases: zero
symbol errors/slips, maximum phase error 0.0442814 UI and interpolation
departure 0.883707 ADC count. This is a digital unit result, not an analog
aperture qualification. PCS clock enables now pass the independent frame,
fault and poisoned-pause regressions. The clock-alignment FIFO also passes
720 frames in native/RTL with fixed-rate enabled input. Composed fixed-sample Ethernet testing now passes 18 phase/rate/role cases,
36 frames and 28,764 bytes, with 796,260 exact native/RTL clock states. This
fixture uses a declared idle-training interval. The first 64-clean-idle
readiness candidate was insufficient: retained earliest-link tests lost both
frames while receive timing still had about 1426 ppm period error. The
[measured timing-confidence gate](../receive_timing_confidence.md) now requires
bounded detector bias, period movement, excitation and absolute error over
accepted observation windows before the PCS idle qualifier can assert ready.
Its standalone native/RTL proof passes 28,429 exact clock states. The maintained
confidence-qualified composition passes all 796,260 clocks and the original
36 frames without an elapsed PMA-enable timer; its receipt is
`out/receive-sample-pcs-qualified/v4/qualification/checks.json`.

The separate earliest-legal-peer regression now passes all thirty rate/phase/noise
cases with native and RTL receive paths: 60 long frames, 91,560 bytes and
1,962,253 identical complete-clock states. Rates cover −1900/−1500/0/+1500/+1900
ppm, delays 0/0.105/0.9 UI, and zero or bounded ±2-count ADC noise. Actual PMA
handshake and the earliest clean SEND_N arm event determine frame launch; no
training timer delays transmission. Independent auditing verifies all 981,134
transmit symbols against the previously IEEE-replayed source stream, including
source index, side, frame-enable and source-byte equality. Receipts and archive
hash audits are in `out/receive-earliest-peer/maintained-v2/`. The transmitter,
PMA control and causal channel remain native test fixtures in this comparison;
it is not a complete RTL PHY or physical clock qualification. The asymmetric
free-running slave clock is a stress stimulus, not recovered transmit-clock
implementation. Seven current targeted digital CTests pass, and the immutable
converter catalog check passes all 48 RTL/package files. Mapped PCS frame,
fault and pause traces also match, with 42,388 IHP cells. Its 4 ns slow-corner cell timing fails at
−3.38 ns slack; the forced-metric selection and trellis arithmetic lie on the
critical path. A retimed selection experiment keeps the existing pipeline
latency and matches all native, RTL and mapped traces with 42,212 cells.
It still fails 4 ns timing at −2.39 ns slack. A separate 7 ns clock study
passes cell-only timing at +0.61 ns slack; that feasibility result does not
waive the failed 4 ns contract or establish routed timing. The new
[symbol clock crossing](../receive_symbol_crossing.md) passes 70 phase/rate
cases and 236,950 exact native/RTL event states, including 70,047 complete
bundles. Its 4 ns ADC / 7 ns PCS boundary is a unit result; clock generation,
physical CDC constraints and full receiver composition remain outstanding.
Slice mismatch,
jitter and SAR setup/hold remain separate physical requirements. A physical
edge-receiver diagnostic exists, but additional PLL feedback circuitry is
deferred until the fixed-sample recovery path is tested.

## Autonomous receiver and clock integration, 2026-10-24

[`AutonomousADCReceivePHY`](../autonomous_adc_receive_phy.md) now connects the
actual thirteen-slice SAR sequencers to fixed-rate timing recovery, a clock
crossing, autonomous cancellation/equalization and PCS. Its interface consumes
comparator decisions; it does not consume far-end training targets. Four
coupled digital fixture cases recover two complete frames and 1,598 bytes each,
with exact native/generated-RTL trajectories. These fixtures model comparator
decisions from held samples. They do not qualify the complete electrical AFE,
automatic gain/offset calibration or the full cable envelope.

The ADC remains at 250 MHz; the separate processing clock is qualified over
132–136 MHz. The carry-save PCS baseline uses exact path comparison and a
six-stage branch-metric pipeline. Its frame, fault and symbol-enable traces
match native C++, generated RTL and 71,945 mapped IHP cells. The slow-corner
136 MHz cell-only estimate passes with 0.85 ns setup slack. Revised placement
buffering initially gave 0.002952305 ns setup slack and −0.215014458 ns hold
slack. Drive-strength repair after hold repair shortened some paths again.
The maintained flow now inserts final hold delay after that repair, then uses
the placement space reserved for late buffers during legalization. The fresh
`build/clock-integration/final-hold-space-receiver/PCSReceiver-a63onlnw`
checkpoint has **0.122084454 ns hold slack** and **0.000614619 ns setup slack**;
slew, capacitance and fanout checks pass. The 136 MHz clock, 0.1 ns uncertainty
and interface constraints are unchanged. The requested 0.15 ns repair margin
is an optimization target, not a relaxed acceptance limit. Setup margin is
very small. This is a placement result, not routed timing closure; detailed
routing, post-route equivalence, extraction and physical verification remain
pending. The routing flow repeats final hold repair using routed estimates
and still requires nonnegative extracted setup and hold slack.

Global-route repair of that baseline subsequently failed setup at −0.450 ns
in `metrics.squared`, despite repairing hold to +0.051 ns at that intermediate
stage. The run was stopped before detailed routing; it produced no qualified
GDS. The new candidate removes conditional sample negation before squaring
and instead reverses the five stored level costs in the next stage, using
`(-sample - level)^2 == (sample + level)^2`. Polarity travels with its accepted
sample and freezes during symbol pauses. Pipeline latency remains six clocks.
The independent native branch-metric oracle passes. Complete receiver frame,
fault and clock-enable trajectories now match in native C++, generated RTL
and 73,085 mapped IHP cells. The slow-corner 136 MHz cell setup estimate improves
from +0.85 ns to **+1.86 ns**, with unchanged constraints and functional evidence
required. Its first placement had −0.021163516 ns setup and +0.117585614 ns
hold slack after legalization. A further setup-reserve and hold repair now
runs in a separate OpenROAD process, preserving both the original placement
and the repaired checkpoint. Restarting before global routing avoids mixing
the placement and routed parasitic estimators' update state. The maintained
`build/clock-integration/polarity-margin-separated/PCSReceiver-g1cdmor3` run
reproduces **+0.160363287 ns setup** and **+0.150055081 ns hold** before routing.
Its global-route estimate subsequently failed setup at −0.115185201 ns while
hold passed at +0.139998347 ns. Detailed routing was stopped to preserve that
checkpoint for repair. Replacing one excessive delay cell preserves hold;
repairing the final loaded network and allowing more setup-repair passes gives
**+0.053054452 ns setup** and **+0.139850020 ns hold** after legalization in
`build/clock-integration/final-network-repair-safe/`. Remaining XOR/XNOR slew
violations still reject that candidate. The standard-cell library has only
one drive strength for those gates. Two output-isolation passes improve setup
to +0.200952142 ns and retain +0.139849916 ns hold, but two remaining output nets
still violate slew. The maintained `finish_timing.tcl` now iterates real output
buffers, setup/hold repair and antenna repair. The v5 run in
`build/clock-integration/receiver-final-routing-v5` passes its final global
estimate with **+0.202502027 ns setup**, **+0.139845580 ns hold**, no slew,
capacitance or fanout violations and zero reported antenna violations.
The combined run was stopped during detailed routing as it exhausted available
RAM. Its repaired global database and interruption receipt are retained.
`receiver-final-routing-v6` resumes that exact checkpoint in a fresh process
without the synthesis/timing state. The maintained large-module flow now
separates global timing repair, detailed routing and extracted qualification
into three processes; two physical-driver guard tests pass for this sequencing.
The failed OpenROAD estimator-state attempts are retained. A new CTest exercises the maintained repair on real IHP cells:
the deliberately loaded XNOR starts with a 1.91 ns transition against 0.5 ns,
and the short path starts at −45.273 ps hold slack. Both failures are repaired;
the final estimated hold margin is +167.015 ps. Four physical-flow integrity
regressions also pass after the driver changes.
The v6 detailed route completed in 6 h 05 min with zero router violations.
Independent extraction gives **+0.055523142 ns hold**, but **−1.623519182 ns
setup** and slew/capacitance violations. Normal and fault replay match native;
the long enable/pause/reset replay initially exceeded its 180 s harness limit.
It subsequently passed all **186,741 events** in 319.626 s with a bounded
900 s limit and exactly the same native trace hash. The maintained receiver
flow now allows 900 s for each complete replay. This does not waive a timing
failure or shorten a test.

The retained diagnosis compares the same 112,496 net names in estimated and
extracted SPEF. For estimated wire loads above 1 fF, extracted/estimated
capacitance ratios are 1.358 median, 2.104 at the 95th percentile and 2.381 at
the 99th percentile. `routing_rc_margin.tcl` applies a 2.5× capacitance margin
only during receiver optimization; extraction restores the original PDK LEF
and uses the unchanged OpenRCX model. Its real OpenROAD CTest verifies scaling,
repeatability, restoration and unchanged acceptance constraints. It caught and
fixed compounding caused by `set_layer_rc` modifying OpenDB technology values.
The interrupted v7 candidate is retained. The v8 run found that omitting
resistance in `set_layer_rc` clears it; the helper now supplies both RC values
in the loaded library units. Its extended regression verifies unchanged wire
resistance as well. A checkpoint after electrical repair preserves buffering
before subsequent timing optimization. Both real-cell CTests pass. The v9
incremental repair was stopped with checkpoints retained: its third saved
stage still has −0.927933335 ns setup under nominal placement estimates,
while subsequent routing is congested. A fresh 1.8 mm receiver placement now
uses timing-driven net weighting and routability-driven spreading at 60%
target density. Timing limits and the mapped functional design are unchanged.
That placement completes with +0.059227291 ns setup, +0.140721887 ns hold,
110,410 legally placed cells and no electrical violations. A fresh routing
run imports its hashed database and repeats all downstream checks. Setup
repair now avoids serial rebuffering; electrical and hold repair still insert
required cells. Global screening restores nominal LEF RC after preserving the
2.5× optimization report. Detailed extracted acceptance remains unchanged.
No new receiver GDS is qualified. These placement estimates do not qualify a
finished ASIC. The 359-file RTL catalog has been
regenerated, and every exported hierarchy passes Verilator lint.

The [clock circuits and their qualification boundaries](clocks.md) are now
implemented. The reference clock passes native/Verilog-A requirements,
comparison and the fresh nominal IHP PSP test. Its process-model output is
250.003564 MHz from 125 MHz; simulated output periods span 3.994359–4.005641 ns.
The process receipt is
`build/1G/IHPSG13G2/tests/results/reference_clock_psp/checks.json`.
This is schematic nominal evidence, not an extracted or noise/PVT jitter
qualification. The processing oscillator passes all six static native,
Verilog-A and nominal PSP trim/load/refinement cases. Its direct generic
comparison also passes. At trim 38528 the native/Verilog-A frequency difference
decreases from 0.835894% to 0.413712% when the native timestep is halved;
all waveform, tuning-voltage and supply comparison limits pass.
Closed processing-clock control passes native and Verilog-A tests using the
achieved oscillator edges. Its direct comparison also passes after rechecking
both full electrical trajectories: frequency difference is 0.213552%, output
period-shape RMS difference is 15.838 mV, and sustained readiness begins on the
same reference cycle. The two autonomous controllers select different trim
codes; accounting for their ideal passive DAC voltages leaves 1.313 µV tuning
voltage disagreement. The first direct-voltage comparison failed because it
did not account for those different commands; its report is retained in
`build/clock-integration/closed-clock-comparison-initial/`. Five guard tests
include an independent nodal solution of the two DAC ladders. The closed-clock
PSP run passed for that controller revision at 134.282632 MHz, trim 38592,
1.197758 V minimum high level and 1.190273 V minimum supply, with all 8,000
reference edges and the final 512 cycles qualified. The completed receipt and
original controller are retained under
`build/clock-integration/clock-before-timing-fix/`; the clock tests require rerunning
after the acquisition-range correction below. These checks do not implement the slave transmitter's
recovered-clock tracking or qualify a routed clock tree. Complete two-port
PHY integration, analog layout and full-chip verification remain unfinished;
there is still no complete repeater GDSII.

The acquisition clock must also meet a faster timing budget than the settled
PCS clock. The nominal PSP acquisition trajectory contains a 3.370812 ns
delivered period (296.664 MHz). A new controller regression maps both domains,
replays native/RTL/mapped/resized trajectories, and checks slow-corner cell
timing at 125 MHz reference and a 400 MHz raw-clock design budget. The original
controller fails with −0.661825299 ns setup slack, despite exact functional
trajectories. The longer independent fixture now covers 192,836 events and
counter wraparound. A separate arithmetic prototype precomputes both search
midpoints and registers counter carry predictions; it preserves the complete
native trajectory and has +0.685320 ns cell-only setup slack. The same 192,836
events also match generated RTL, mapped IHP gates and resized gates. That
arithmetic is now in the maintained controller. Both fresh digital CTests pass:
1,474 mapped cells, exact complete trajectories and +0.685319841 ns cell setup
slack. All 359 generated RTL/package files have been regenerated and their
complete hierarchies linted. The fresh native closed-circuit test passes;
The queued Verilog-A and partial PSP runs were deliberately stopped after a
fast-corner diagnostic invalidated that acquisition budget; their interruption
receipts and source snapshots are under
`build/clock-integration/before-clock-envelope-fix/`.
At maximum trim, FF/1.32 V/−40 C with the declared 50 fF stage capacitances and
20 fF output load produces **645.155 MHz** and a **1.550016 ns minimum period**.
The diagnostic is under `build/clock-integration/clock-fast-limit/`; it is one
operating point, not a complete oscillator envelope. The revised controller now passes both maintained digital CTests at an
**800 MHz raw-clock budget**: **+0.172788680 ns cell setup**, **1,497 cells**, and
**292,092 exact C++/RTL/mapped/resized events**. It uses a continuously running
counter with two-bit predicted carries, delayed Gray conversion, a pipelined
heartbeat comparison, unary silence history and asynchronous reset assertion.
The expanded fixture covers five tunable and three rejected curves, startup
rates up to 780 MHz, wraparound and cold/warm reset without either clock.
The maintained source and generated RTL have been updated. All twelve PSP startup/load/PVT points and six acquisition-checker guards
pass. The fastest complete startup period corresponds to 645.421 MHz. Slow
or stopped midscale is allowed because the reference-domain search raises
trim; maximum trim must start and sustain a clock above 136 MHz. The corrected
checker fully revalidated ten hash-matched trajectories and simulated the two
slow-corner midscale cases fresh. Fresh
closed-circuit qualification is required for this revision. The cell proof
still excludes routed hold, CDC, reset release and receiver clock delivery.
The new controller physical flow preserves both clock domains and times
`ready_pcs_out` in the raw-clock domain. Its input snapshot requires exact
native/RTL/mapped/resized traces and tracked mapped JSON as well as Verilog.
Four snapshot guards and the existing physical-driver regressions pass. A
controller physical run completed under
`build/clock-integration/controller-decision-watchdog-physical/`. The earlier bounded
candidate completed routing and exact replay, but failed extracted setup at
−1.434322 ns (hold +0.067394 ns), so no qualified GDS was produced. The revised
controller registers Gray decoding and readiness qualification. Its clock tree
removes redundant buffer levels and advances the readiness branch. Placement
estimates pass at +0.061829 ns setup and +0.069796 ns hold. The capacitance budget
is now 0.1 pF with fanout eight; the 800 MHz acquisition requirement, uncertainty
and output delay remain unchanged. Four real-cell clock-tree regressions pass.
The readiness revision subsequently completed detailed routing with zero router
violations and exact functional replay. Extracted hold passes at +0.066345 ns,
and raw-output setup at +0.041346 ns, but the reference measurement/decision
path fails setup at −0.169423 ns. Its failed receipt and source snapshot remain
under `build/clock-integration/controller-ready-physical/`. The maintained
controller now captures the complete 128-cycle measurement and decides on the
next reference edge. Four fresh CTests pass, including all 292,092 events in
native/RTL/mapped/resized flows. A negative regression reproduced lost watchdog protection when the reference
stopped in the new decision state. That intermediate physical run was stopped
before qualification. The maintained fix keeps the watchdog armed there and
registers its acquisition level before crossing domains; the new boundary-loss
fixture passes in all four digital flows. The final 280 × 280 µm controller GDS passes its component checks:
extracted setup +0.052648444 ns, hold +0.069256634 ns, no electrical violations,
exact native/routed replay, zero full-deck KLayout DRC violations, and transistor
LVS matching 47 circuits, 40 ports and 20,432 devices. The independent extracted
Gray-bus CTest passes all 22 bits at slow/fast corners; worst conservative arrival
windows are 0.6918 ns and 0.4703 ns. This is a controller macro, with nominal
routed RC and no post-fill coupling, reset-release or metastability-MTBF claim.
The updated closed-clock C++RTL and Verilog-A flows both pass independently,
at 134.245431 MHz and 134.532411 MHz respectively. The nominal PSP loop also
passes at 134.282637 MHz with trim 38592 and 512 final qualified reference edges.
A bounded recheck verifies all 8,671,136 samples from the completed 64 µs
simulation. The native/Verilog-A comparison passes with 0.21332% frequency
difference and 38.079 mV output-period RMS difference. Its initial list-based
checker exceeded its memory cap; the failed receipt and original reader are
preserved. The original waiting PSP checker/CTest queue was stopped after the
independent compact recheck passed. Exact original physical-flow scripts
are retained by their recorded hashes under
`build/clock-integration/retained-flow-sources/`, since subsequent receiver
optimization changes are not the scripts used to qualify this controller.
No complete repeater GDSII is qualified.

The preceding controller GDS used a 5 fF output load. Charge integration of
the actual admission gate, at both held clock levels and all three declared
process corners, finds a maximum equivalent request capacitance of 52.790 fF.
The maintained input-charge checker passes all six trajectories. Controller
implementation now reserves 80 fF for that gate and its interconnect; the
100 fF total-net acceptance limit remains unchanged. The first loaded route
failed global timing because its 72 fF optimization target was smaller than
the external load. A corrected 95 fF optimization target allowed routing, but
extracted setup still failed at −0.233637109 ns, with positive hold slack.
That failed layout and its exact functional replay remain recorded under
`build/clock-integration/controller-loaded-admission-v2/`.
The next candidate factors the readiness AND into an enable/reference AND,
a raw-qualifier NAND and an output inverter, preserving asynchronous withdrawal.
Its real-cell Boolean proof and four malformed-topology rejection cases pass.
The controller also uses 2.5 times LEF wire capacitance during optimization;
independent extraction restores original process parameters. The first factored run exposed a long serial buffer
chain introduced by setup rebuffering; this optimization is now disabled for
the small controller, while electrical and hold repair retain their buffers.
The readiness register has a dedicated buffer-8 clock branch to balance its
input and output paths. Five real-cell clock-tree cases pass, including
rejection of a conflicting branch before mutation. The flow records the conservative optimization report,
then restores original LEF capacitance for global screening before independent
detailed-route extraction. A real-cell regression checks that the margin does
not leak into the final timing view. The previous shared-root candidate failed
extracted setup by 7.528 ps; a buffer-4 branch failed by 9.838 ps, with positive
hold in each case. A separate sizing study selected buffer-8. Its current
candidate completes detailed routing with **+0.037747696 ns extracted setup**
and **+0.064936139 ns extracted hold**, with no electrical violations. Its
280 × 280 µm GDS passes the complete DRC deck with zero violations and
transistor LVS (46 circuits, 40 ports, 20,404 devices). Fresh controller
functional, mapped/resized and routed proofs match all 292,092 events. The
extracted Gray-bus crossing audit also passes. The receipt and original flow
source snapshots are retained under
`build/clock-integration/controller-loaded-admission-v7/ProcessingClockControl-1jbz89kz/physical/`.
Subsequent receiver routing-script changes do not modify this qualified
artifact or renew its provenance. Post-fill coupling, reset release and the
connected receiver clock tree still require qualification.

The new 38-MOS `processing_clock_gate` passes both registered CTests: six
checker guards and the complete electrical matrix. Native, Verilog-A and
IHP TT/1.2 V/27 C, SS/1.08 V/125 C and FF/1.32 V/−40 C each deliver the
32 expected complete pulses while blocking all 32 acquisition cycles. The
slow/hot case has a 3.684 ns minimum high pulse and 178.242 ps maximum edge
delay at 20 fF load. Native/Verilog-A RMS disagreement is 18.392 mV at the
output and 11.386 mV at the stored request, below the unchanged 20 mV limits.
The comparison excludes one initial native sample before SPICE's first
positive integration time; both full-duration coverage checks still apply.
The initial comparison-boundary failure is retained under
`build/clock-integration/gate-proof`; the fresh passing receipt is under
`build/1G/IHPSG13G2/tests/results/processing_clock_gate`.
Its generated Verilog-A model is included in the maintained catalog.
This is standalone schematic evidence: controller integration, latch-aperture
timing, clock-loss/restart handling, physical realization and the receiver's
extracted clock tree remain open.

`processing_clock_delivery.hpp` now connects the oscillator's RAW node to the
physical admission gate, sharing its supply and actual transistor loading.
The new closed-loop bench drives ADMIT from `ProcessingClockControl` readiness
in the raw-clock domain and loads the delivered output with 200 fF. Its native
build, Verilog-A export and 89-MOS process binding succeed. Five checker cases
pass, including lost pulses, cut pulses and output before admission. The complete
C++RTL test passes at 134.132254 MHz: 3,833 complete delivered pulses, 3.713606 ns
minimum high pulse and 36.706 ps maximum edge delay. Verilog-A passes at
134.466526 MHz with 3,980 complete delivered pulses, 3.716310 ns minimum high
pulse and 25.537 ps maximum edge delay. Its initial checker exhausted memory
after simulation completed. A packed-double reader now retains every sample
without Python objects for every value; four reader regressions pass. Separate
recheck receipts validate all 1,324,173 native and 10,647,127 Verilog-A samples,
the original simulation hashes and the unchanged electrical limits. The failed
original receipt is retained. The independent generic comparison passes:
frequency differs by 0.24859%, delivered-clock waveform RMS by 20.342 mV,
and sustained readiness by one 128-reference-cycle measurement window.
The connected PSP acquisition remains running. This still excludes the actual receiver clock tree, reset distribution,
clock-loss/restart qualification, latch aperture characterization and layout.

The new `PHYResetControl` connects the three-domain release chains to enable,
power-good and both clock qualifications. Its generated RTL binds the combined
request to the child's asynchronous-reset pin. Both reset CTests pass: the
standalone release block matches 30,720 events, and the connected control
matches 153,600 events across native, generated RTL and mapped IHP cells.
The latter uses 21 cells, including six resettable registers. Tests include
brief qualification withdrawals while every destination clock is stopped.
This does not yet connect reset to the complete receive pipeline or qualify
physical reset routing, pulse-width limits, recovery/removal or metastability.

The connected clock-admission layout now uses actual MOS fingers and MIM
plates, including two physical plates for each 2 fF series element. Strict
KLayout LVS matches 104 devices; the complete DRC run has only isolated-window
minimum-density violations. Local geometry, pin and antenna checks pass, and
removing an input via fails LVS as required. This is an unfilled standalone
macro, not a complete chip. The [analog extraction flow](analog-extraction.md)
preserves 46 MOS fingers and 28 MIM plates. Its capacitance-conservation and
real MIM extraction guards pass. Placement now puts capacitors near their
connected devices and reduces the enable wiring load from 80.735 to 15.485 fF.
The controller's real output masters driving the extracted gate and a 200 fF
output load pass TT/27 °C/1.2 V, SS/125 °C/1.08 V and FF/−40 °C/1.32 V.
Each produces 32 complete intended pulses; worst delay is 352.431 ps against
the unchanged 400 ps limit. Extracted input-charge requalification now passes
all six held-clock/process cases at 100 ps command edges. Maximum equivalent
load is 71.509 fF; the remaining 8.491 fF must cover the external route from the
controller. Its receipt is `build/clock-integration/clock-gate-placed-charge-v2/checks.json`.
Field-solver calibration, clock-tree/reset integration and full-chip placement/fill remain
open. See `build/clock-integration/clock-gate-placed-controller-pex/checks.json`.
