# Physical integration

## Pad-connected electrical hierarchy

`repeater_analog` has distinct direct TX terminals (`P`, `N`) and protected
RX terminals (`SENSE_P`, `SENSE_N`). `pad_integration.py` connects them to the
actual IHP padframe CDL: TX uses `pad`, while RX and FLP detection use `padres`
behind secondary protection. They remain electrically coupled through the
pad's transistor/resistor network. The input-pad outputs drive reference,
reset and enable; the status output drives an output pad. Core logic uses
`vdd`, the analog supply uses `iovdd`, and four physical corner straps join
VSS/IOVSS. This wiring does not establish ESD or signal-integrity qualification.

Run `cmake --build build/behavior --target repeater-pad-integration` to build
and lint `RepeaterPadConnected.sv` against the converted digital hierarchy
and the analog/pad implementation interfaces. `analog.pdk.1g_repeater.pad_integration`
registers the same flow with CTest, including negative pad/lane/supply tests.
For reuse of an existing electrical proof, invoke `pad_integration.py
--electrical <electrical-run> --pdk pdk/IHPSG13G2 --work <pad-run>`.

The resulting `connections` report lists unresolved core inputs, including
the two TX clocks, aperture-aligned local history, calibration and startup
controls. These are not package pins or silently tied constants. Physical
mapping of every analog element, those producers, core-to-pad routing and
whole-chip verification are still required. The 6000 µm pad opening is a
provisional integration boundary, not an optimized or routed full-chip GDS.

## Placement without fixed digital blocks

`repeater-unified-placement` starts from the connected `RepeaterDigital`
hierarchy and its native/RTL regression. It maps the two frame FIFOs to eight
physical SRAMs and maps the remaining logic. OpenROAD expands the mapped
logical hierarchy into individual physical cells during `link_design`.
`unified_synthesis.py` rejects missing memories, missing port/control logic,
unmapped leaves and logical blackboxes in the placement input.
Yosys resolves the complete parameterized hierarchy before it is partitioned
into separate RTLIL synthesis jobs. This preserves the distinct two-bit and
four-bit control crossings and the thirteen-slice ADC banks. Successful jobs
have input/output hash receipts and can be reused after a later failure. The
split bounds compiler memory; it introduces no physical regions or fences.
Keeping the mapped Verilog hierarchy avoids a second, memory-intensive
Yosys flattening pass. The placement inventory rejects whole logical blocks
as physical masters; only the eight SRAMs remain hard macros.

`unified_place.py` and `unified_place.tcl` create a fresh common placement area
from actual cell area. They do not import old block GDS/ODB files, locations,
or module fences. Only SRAM masters retain fixed geometry. The placer can
intermix PHY, DSP, PCS and forwarding cells, and retains floorplan, global and
detailed placement checkpoints. Initial timing estimates are reported even
when they fail; they do not prevent saving placement.

For a stopped run that saved `floorplan.odb`, run `unified_checkpoint.py
--proof <run>/checks.json` before changing the placement recipe. This seals
the database, inventory, constraints and physical dependencies. Resume into
a new directory with `unified_place.py --resume-floorplan
<run>/floorplan-checks.json` and the same mapped `--proof`, PDK and tools.
`--memory-mib` sets the process limit. Under limited memory,
`--geometric-only --bin-grid-count 256` saves legal cell placement without
timing/congestion optimization; its receipt explicitly records timing as
`not_run`. Timing closure and routing remain required after this checkpoint.
`unified_timing.py --proof <placed-run>/checks.json --pdk <pdk> --tools <tools>
--work <new-timing-run>` reads the saved database in a separate process and
reports placement-based setup/hold estimates and constraint coverage. For the
default build directories, use `repeater-unified-timing` after placement. This
target does not resynthesize or replace the existing placement.
Adding `--repair-electrical` buffers high-fanout/load nets, resizes drivers,
repairs tie fanout and legalizes the modified cells in a new run directory.
It saves the repaired database before reporting timing; the prior placement
remains available. This step does not claim clock-tree or routed timing closure.

When a whole-design electrical pass is too slow, `unified_net_repair.py`
accepts repeated `--driver <physical-instance>` arguments and repairs only
those output nets in the saved `--proof` placement. Each completed net repair
atomically saves `repair.odb`; the final `placed.odb` requires legalization.
The report distinguishes an intermediate checkpoint from a legal placement.
`nets.tsv` records terminal counts and elapsed seconds per repaired net.
All-net electrical closure, CTS and routing still remain after this bounded
step. No timing constraint is relaxed by selecting a repair batch.
Repair, placement rechecking and timing use separate processes to keep their
peak memory bounded. Text DEF/Verilog export is optional (`--export-text`);
the default retains the native database. Run `unified_timing.py` on the new
placement receipt to obtain timing estimates. A stopped run with complete
ODB/DEF/Verilog can be recovered with `unified_recover_placement.py --proof
<failed-run>/checks.json --tools <tools>`; it independently reloads and checks
placement before issuing a separate receipt.

Before changing a repair recipe, `unified_archive_recipe.py --proof
<placed-run>/placement-checks.json` seals its executed version into
content-addressed copies. Use the resulting `sealed-placement-checks.json`
for subsequent work. RTL, circuit inputs, constraints, libraries and geometry
remain checked; editing an orchestration script does not require discarding
a previously validated physical database.

Run from a configured build with
`cmake --build build/behavior --target repeater-unified-placement`.
The placement destination must be new so an existing checkpoint is not
overwritten. This target currently covers digital placement only: analog
device placement with matching constraints, pads, power distribution, clock
trees, routing and whole-chip verification still need integration. It does
not publish a completed GDS or replace the existing images.
`repeater-unified-digital-gds` additionally streams the placement using actual
IHP standard-cell and SRAM GDS libraries, checks the saved hierarchy, and
renders its geometry. Those artifacts are explicitly unrouted digital
checkpoints, not the full analog/pad chip.

## Existing published floorplan

`1G/IHPSG13G2/results/repeater_development.gds.gz` and
`repeater_full_resolution.png` currently show a floorplan. The enclosing I/O
ring does not electrically connect the interior blocks. A completed repeater
requires the production instance inventory and actual routed nets; the review
collection includes alternatives and individual constituent circuits.

The current composition uses a **5.04 × 5.04 mm die**, reduced from
5.44 × 5.44 mm (14.17% less die area). Nineteen interior candidates occupy
74.90% of the 4.40 mm square core by their bounding footprints; this is not
transistor or standard-cell utilization. The plan retains 100 µm channels
and core margins. Receive DSP sits above PCS beside forwarding memory,
receiver analog circuits follow the north edge, transmit circuits occupy
the east band, and the processing clock and controller share a routed subsystem.
This subsystem replaces the separate clock-delivery and controller candidates;
the current 1200 µm autonomous DSP replaces the older supervised filter.

`development_floorplan.json` records coordinates and explicit replacements.
The publication script checks preserved/replacement geometry, exact inventory,
GDS round trip, pad intrusion, core containment and channels before replacing
the combined GDS and images. Its CTest includes missing/partial replacement
rejection and repeatable import. These checks establish composition only;
full-chip wiring, DRC/LVS, extracted timing and production replication remain
unfinished.

## Connected processing clock

`integrate_processing_clock.py` now implements the physical connections from
`1G/IHPSG13G2/processing_clock_delivery.hpp`. It places the existing oscillator
and admission gate face-to-face, preserves both device layouts, and adds real
Metal4/Metal5 wires and Via4 cuts for RAW, VDD and VSS. RAW is 52.84 µm long.
Both source schematics explicitly share the process substrate, PSUB. This is
an internal substrate connection, not an invented metal short to VSS.

The new CTest test, `analog.pdk.1g_repeater.processing_clock_integration`,
passes transistor LVS with **398 devices, 209 matched circuits and 22 external
ports**. Removing the inter-macro clock conductor makes LVS fail as required.
The test binds the source GDS, independent schematics, generator, rules and
comparison databases by their hashes. No ideal wire in a simulator substitutes
for these physical connections.

A full PDK DRC run of the original unfilled pair completed all 37 main rule tables, maximum checks and
density. It found no local geometry violations and nine minimum-density
violations in this unfilled macro. The complete DRC result therefore still
fails; fill and rechecking remain required. The measured receipt is
`build/clock-integration/physical-delivery-drc/checks.json`.

```sh
cmake -S . -B build/behavior
ctest --test-dir build/behavior \
  -R '^analog.pdk.1g_repeater.processing_clock_integration$' --output-on-failure
```

Run from `analog/` with the prepared IHP PDK and KLayout toolchain. Each test
creates a fresh directory beneath
`build/behavior/pdk/1G_repeater_IHPSG13G2/tests/processing-clock-integration/`.
Its `checks.json` points to the actual `delivery.gds`, transistor schematic,
positive LVS database and disconnected negative control.

The clock controller, receiver loads and padframe remain outside this macro.
Inter-macro RC, loaded electrical performance and full physical qualification
remain required. This connection proof does not qualify the whole chip, and
the public full-die PNG has not been relabeled as an integrated ASIC.

## Compact connected placement

`compact_processing_clock.py` places the oscillator and admission gate's 189
primitive instances in one placement domain. `compact_analog_rows.py` replaces
the long buses with compact rows, row-local tracks and shared inter-row conductors.
It preserves every primitive's geometry. Wide supply-trunk crossings use
25-cut via arrays. The independent LVS reference remains the original two
transistor schematics connected according to `processing_clock_delivery.hpp`.

The candidate is 632.26 × 517.20 µm, a 30.34% reduction in bounding area against
the connected strip placement. This measures the clock macro, not the chip.
Compaction changes parasitic resistance and capacitance, so previous electrical
results do not qualify the new routing.

The first compact placement reduced area by 37.92% but increased RAW parasitic
capacitance from 60.57 to 181.25 fF. That revision failed the pin-load screen.
The current placer gives RAW higher weight and keeps its driver and receivers
in one row. RAW is now 38.16 fF (37.00% below the original), ADMIT is 20.23 fF
(4.47% lower) and OUT is 15.79 fF (28.03% lower). These are extracted parasitics,
not total device loading or a waveform/timing pass. `compare_clock_compaction.py`
rejects increased loading on any of these three terminals.

The filled compact candidate passes full PDK DRC with zero violations and
transistor LVS with 398 devices and 22 ports. The deliberately opened clock
trunk fails LVS. Its receipt is
`build/clock-integration/compact-clock-priority-qualification/checks.json`; the
published macro GDS and 3600 × 3100 mask image are in
`1G/IHPSG13G2/results/processing_clock_delivery.*`.

Full-precision Magic extraction also passes the existing device-range and
capacitance-conservation guards: 42,441 distributed resistors and 3,393
capacitors, with 3.559389 pF total conserved parasitic capacitance. The extractor
identifies VSS as its substrate reference in `ExtractedAnalog.ext`; the
KLayout reference retains its explicit internal shared PSUB topology. No metal
short was added to reconcile names. The extraction receipt is
`build/clock-integration/compact-clock-priority-extraction/extraction.json`.
Electrical waveform qualification of this new routing remains open.

```sh
# Small partition/port/supply-routing guards.
ctest --test-dir build/behavior \
  -R '^analog.pdk.1g_repeater.compact_analog_rows$' --output-on-failure

# Fresh placement, complete transistor LVS and an intentionally opened RAW conductor.
ctest --test-dir build/behavior \
  -R '^analog.pdk.1g_repeater.compact_processing_clock$' --output-on-failure

# Include PDK fill and all DRC tables, maximum, density and antenna checks.
pdk/IHPSG13G2/.cpphdl/venv/bin/python \
  pdk/1G_repeater_IHPSG13G2/tests/compact_processing_clock.py \
  --work build/compact-clock-full --full-drc
```

Each verification run records hashes of its geometry, original schematics,
placement/router sources, PDK rules, tool and comparison reports. The negative
test must finish a real LVS mismatch; a crashed tool cannot pass it.

## Routed controller feedback

`integrate_clock_control.py` imports the actual oscillator/admission GDS and
the routed controller GDS, derives physical pin/obstruction abstracts, and
uses OpenROAD for detailed inter-macro routing. It preserves source geometry
when replacing the routing abstracts with GDS. Port-only Liberty definitions
retain hard macros during linking; they provide no timing qualification.

| Physical connection | Direction | Count |
| --- | --- | ---: |
| RAW → pcs_clk | Oscillator to controller | 1 |
| ready_pcs_out → ADMIT | Controller to admission latch | 1 |
| clear_out → CLEAR | Controller to oscillator reset | 1 |
| coarse_n_out → COARSE_N | Controller to coarse DAC | 8 |
| fine_n_out → FINE_N | Controller to fine DAC | 8 |
| VDD and VSS | Shared supply wiring | 2 |

The first routed candidate (`clock-control-integration/route-4`, below
`build/clock-integration/`) passed extraction of all 21 physical connections,
with distinct interface nets remaining separate. Cutting the inter-macro
channel caused the connectivity test to fail. Its independent transistor LVS
passed with 20,938 devices. The analog process-substrate terminal remains
distinct inside its macro and meets the controller's substrate/VSS contacts
at system scope; no imaginary internal substrate short is substituted.

The compact placement rotates the controller below the analog macro, aligning
the RAW terminal. Terminal Manhattan distance decreases from 571.82 µm to
198.92 µm. This is a placement metric, not extracted delay or capacitance.
External interface pins stay on the real macro terminals. Qualification uses
the resulting GDS, independently composed original transistor schematics,
an opened-routing control, and all PDK DRC tables after fill. The scripts
`tests/clock_control_connections.py`, `tests/clock_control_lvs.py`, and
`tests/clock_control_physical.py` retain receipts for these checks. The CTest
`analog.pdk.1g_repeater.clock_control_guards` covers opens, shorts, missing
metal/ports and the rotated physical terminal mapping.

The first filled compact candidate (`qualification-7`) passed physical
connectivity, its opened-wire control, density and antenna checks. Full PDK
DRC found two `M4.e` errors: 0.22 µm spacing beside existing wide analog metal,
where 0.24 µm is required. The router's own check had reported zero errors.
The routing abstract now reserves another 40 nm around Metal2–5 obstructions;
the source device geometry is preserved and tested. Filled-layout transistor
extraction also exceeded the original 300-second limit; the runner now exposes
a bounded timeout, defaulting to 900 seconds. A timeout never counts as LVS
success.

The repaired `route-8` candidate passed `qualification-8`: **zero full PDK
DRC violations**, including all 37 main tables, maximum rules, density and
antenna; **20,938-device transistor LVS**, 258 matched circuits and 22 top
ports; and all 21 physical connections with a rejected opened-wire control.
Filled-layout LVS took 482 seconds. The published `processing_clock_system.*`
GDS, image and receipt retain the 632.26 × 917.20 µm connected subsystem.

This joins one subsystem. It does not establish the receiver clock-tree load,
closed-loop electrical performance, supply integrity or full-chip integration.

To route and qualify the retained clock/controller checkpoints, run from
`analog/` with the prepared PDK and toolchain. Use fresh work directories;
the default checkpoint paths are `CLOCK` and `CONTROL` in the integrator.

```sh
export PYTHONPATH=out/pdf-renderer
pdk/IHPSG13G2/.cpphdl/venv/bin/python \
  pdk/1G_repeater_IHPSG13G2/integrate_clock_control.py \
  --work build/clock-control-route
pdk/IHPSG13G2/.cpphdl/venv/bin/python \
  pdk/1G_repeater_IHPSG13G2/tests/clock_control_physical.py \
  --implementation build/clock-control-route --work build/clock-control-checks
pdk/IHPSG13G2/.cpphdl/venv/bin/python \
  pdk/1G_repeater_IHPSG13G2/package_clock_control.py \
  --checks build/clock-control-checks/checks.json
```

Publication requires all physical checks to pass and every recorded source
and artifact hash to remain unchanged. The subsystem image includes actual
upper-metal geometry and a closer view of the RAW terminals.

## Production instance accounting

The current review floorplan is not a production instance list:

| Function | Review instances | Required instances | Physical integration issue |
| --- | ---: | ---: | --- |
| Receive PCS | 1 | 2 | One per full-duplex port |
| Adaptive receive filter | 1 | 8 | Four autonomous lanes per port required |
| Processing clock and controller | 1 connected subsystem | 1 connected subsystem | DRC/LVS pass; loaded timing and receiver tree open |
| Reference clock | 2 alternatives | One selected implementation | Electrical qualification still required |
| Forwarding | Old core plus flow-control candidate | One selected implementation | Flow-control path must replace old core |

`AutonomousReceivePCS` instantiates `AutonomousReceiveFilter filters[4]`.
Its `mode_in` selects echo acquisition, PAM3 adaptation, PAM5 adaptation or
coefficient hold. The earlier published `ReceiveFilter` wrapped
`PipelinedReceiveFilter`, which ties autonomous acquisition off and takes a
supervised `desired_in`. That master cannot simply be replicated eight times
as the production autonomous receiver.

`1G/autonomous_receive_datapath.hpp` now provides the physical lane interface
around the actual `AutonomousReceiveFilter`, retaining `mode_in` while removing
the 432 coefficient probe pins from the physical top. Its C++ model, generated
RTL, Yosys process/memory lowering and IHP mapped gates match exactly across
589,833 cycles. Each runs the independent channel checks: 124,280 error-free
held-out symbols, plus a disabled-adaptation control producing 6,835 errors.
The initial mapped design had 28,391 cells. Mapping does not qualify physical timing.

OpenROAD has placed this autonomous lane and built its clock tree in a 1200 µm
square. Placement estimates are setup −0.158159 ns and hold +0.141842 ns; setup
still fails. The checkpoint is retained at
`build/clock-integration/autonomous-filter-physical/AutonomousReceiveDatapath-im8k4dkg/`.
Routing/timing repair resumed that database under
`build/clock-integration/autonomous-filter-routed/AutonomousReceiveDatapath-eqwqsbvc/`.
It retained `finished_global.odb`, with setup −2.615180 ns and hold +0.150596 ns;
electrical and antenna checks have no violations. Setup still fails, so the
flow correctly stopped before detailed routing. The slow path crosses output
quantization and training-error calculation.

`ReceiveFilterPipelineCore::bounded_quantized_value` now bounds the observation
before rounding, removing the wide negation path. The output and saturated
training error remain identical for all 2³⁶ accumulator/mode/target combinations,
as proved by `filter_quantizer_formal.py` against signed division. The actual
converted function is used in the proof. Native/RTL channel tests and the
mapped CTest pass for this revision, with 28,318 mapped cells. The mapped CTest
includes the formal proof before synthesis. Physical implementation completed
global-route timing repair under
`build/clock-integration/autonomous-filter-quantized/AutonomousReceiveDatapath-kz4se59u/`;
setup improved to −1.064291 ns, with hold +0.133144 ns and no electrical or
antenna violations. Setup still fails, so detailed routing did not start.
The remaining worst path is `products[548]` to `sum4[165]`; the saved
`finished_global.odb` retains the placed/clocked candidate for further repair.
The compact floorplan selects this autonomous candidate instead of the older
supervised filter. Exporting it does not turn global-route guides into metal.

The root-visible build target reproduces functional mapping before physical
implementation, and stops on any failed phase:

```sh
cmake --build build/behavior --target one-g-autonomous-receive-filter-gds
```

The faster lane checks are `analog.1g.autonomous_receive_datapath.native` and
`.rtl`; `.mapped` additionally checks Yosys lowering and IHP gates. The original
filter unit test retains all coefficient probes and its state-level checks.

Using just the displayed 2400 µm receiver, 1200 µm filter and 2000 × 1200 µm
forwarding footprints, two receivers plus eight filters plus forwarding total
25.44 mm². This already exceeds the current 19.36 mm² core opening, before the
analog front ends, clocks, pad clearances and routing. This is a footprint
screen using existing candidates, not a final area estimate: the autonomous
filter still requires timing closure and detailed routing. A smaller-looking
review image would conceal missing instances rather than establish a compact
working die.

## Connected digital hierarchy

`repeater_digital.hpp` now instantiates two real `RepeaterPort` hierarchies and
connects their decoded receive words through `FlowControlledRepeater` to their
transmit PCS inputs. This replaces the 21-flop `RepeaterCore` as the digital
integration input. It does not replace the published floorplan with routed
chip geometry.

The elaborated inventory contains 104 SAR controllers, eight adaptive receive
filters, two receive PCS, two transmit PCS, eight PAM5 driver controllers and
eight logical frame-memory banks. The two TX domains remain separate. A raw
processing-oscillator clock reaches its controller while the delivered PCS
clock can be gated. Acknowledged control crossings preserve role/mode words;
physical constraints for their bundled data remain required.

The reproducible checks are:

```sh
cmake -S . -B build/behavior
ctest --test-dir build/behavior \
  -R 'analog\.1g\.control_crossing\.rtl|analog\.pdk\.1g_repeater\.digital_integration' \
  --output-on-failure
```

Run from `analog/`. `flow.py --target digital --work build/repeater-digital`
performs the same integration checks. `--target chip` performs those checks
before reporting the remaining physical blockers. It must still return a
nonzero status while the electrical chip is incomplete.

The tests compare native and RTL startup behavior and inspect the elaborated
netlist. They reject a missing PHY/SAR, a swapped forwarding input and shared
TX clocks. They do not yet qualify complete frame transfer through both
physical ports. Remaining top-level bindings include slave TX-clock recovery,
aperture-aligned echo history, loaded analog sampling/evaluation qualification,
calibration/configuration, physical rails and pads. These are real missing circuits or
connections, not a layout-presentation task.

The next integration stage now connects the actual analog IC hierarchy to
these digital ports. See [electrical-integration.md](electrical-integration.md)
for the circuit inventory, generated connected netlist, checks and remaining
physical boundaries. This establishes schematic connectivity; it does not
promote the earlier review GDS to a routed chip.
