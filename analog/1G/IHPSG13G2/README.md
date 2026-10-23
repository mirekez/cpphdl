# IHP SG13G2 implementations

Keep process-specific cells, their tests and their generated counterparts here.
Generic PHY topology remains in `1G/`; downloaded foundry data remains in
[`analog/pdk/IHPSG13G2/`](../../pdk/README.md).

Physical analog implementation starts with the [connected MOS layout
generator](mos-layout.md), the [sampling capacitor layout](capacitor-layout.md)
and the [routed C++ inverter](inverter-layout.md).
Their tests distinguish local macro geometry and connectivity from full-chip
density, extracted performance and chip signoff.
The [area measurement tool](mapped-area.md) reports checked cell counts,
Liberty areas and actual LEF placement footprints separately from die area.

**Process requalification:** earlier transistor fixtures used provisional wide
single-finger devices. The pinned PSP headers specify at most 10 µm per finger.
`mos_geometry.py` now preserves total width and terminals while selecting a
valid finger count; explicit invalid choices are rejected. The historical
process results below require fresh runs with these corrected geometries;
the amplifier, comparator, track/hold, nominal weighted driver and 200 fF
clock-interface results have now been requalified.
Each new deck retains its actual inventory in `mos_geometry.json`. Generic
C++RTL/Verilog-A same-equation results are a separate verification boundary.

`current_steering_driver.hpp` declares a four-cell unary differential output
stage as an `IC`. Each cell adds two NMOS devices sharing a tail. Four ideal
supply-referenced tail biases draw 5 mA each. Two 50 Ohm pullups and an external
100 Ohm differential load give 0.25 V per PAM level, with a nominal 2.8 V output
common mode at a 3.3 V supply. Gate controls are complementary 0/3.3 V signals.
Each output has an explicit 0.5 pF capacitor.

The C++ and generated Verilog-A counterpart use the framework's **generic
square-law channel** with beta 20 mA/V² and threshold 0.7 V. These are prototype
parameters, not imported IHP process parameters. `generated/OneGMOSLineDriver.va`
is the exported same-equation counterpart for solver regression.

`tests/driver.py` creates the same steering topology using the actual IHP
`sg13_hv_nmos` four-terminal subcircuit, with W = 100 um and L = 0.45 um, body
connected to ground. It loads `cornerMOShv.lib` in the prepared PDK and runs
125 MBd waveforms in ngspice using the compiled PSP models. TT, SS and FF pass
the same five-level and common-mode absolute checker as the two prototype flows.
The PDK tests also check tail headroom, supply current and settling 1 ns after
the control ramp. They are enabled when the prepared model binaries are present.

This establishes a tested process-specific topology, not a foundry-characterized
Ethernet output cell. The tail bias generators and gate drivers remain ideal;
there is no transistor bias reference, 1.2-to-3.3 V level shifter, ESD, magnetics,
layout, mismatch/noise analysis or extracted parasitic model. Its 2.8 V common
mode also needs an explicit interface design before replacing the generic
system driver's 1.65 V common mode. It is not silently substituted into the link.

```bash
ctest --test-dir ~/cpphdl/analog/build -R '^analog\.1g\.(mos_driver|ihp_driver)\.' --output-on-failure
```

PDK setup: `../../pdk/.prepare_IHPSG13G2.sh`. To select another prepared location,
configure CMake with `-DCPPHDL_IHP_PDK_ROOT=/absolute/path/to/IHP-Open-PDK`.
## MOS bias and trim implementation

[biased_driver.hpp](biased_driver.hpp) adds a resistor/diode-connected MOS bias,
physical MOS tail sinks and a PMOS-switched six-bit resistor bank. Read
[the circuit and validation contract](biased_driver.md) for sizes, trim codes,
native/Verilog-A comparison and PSP process-corner results. The older
`current_steering_driver` remains available as an explicit ideal-bias reference.

## Receive-amplifier process experiment

`tests/amplifier.py` characterizes DC, AC and small-signal noise of the basic resistor-loaded
NMOS amplifier with actual `sg13_hv_nmos` PSP devices, W = 50 µm, L = 0.45 µm
and bodies at ground. Drain loads are 1 kOhm, source degeneration 50 Ohm and
the shared tail resistor 150 Ohm. Explicit output/tail/gate capacitors remain;
the PSP devices additionally contribute their intrinsic charge. The generic
square-law class and the PSP experiment therefore have different equations.

All 54 DC/small-signal cases pass: TT/SS/FF, −40/27/125 °C, 2.97/3.3/3.63 V
and unloaded/2 kOhm differential load. The input common mode is half the supply,
and differential input spans ±80 mV. With valid five-finger input devices,
unloaded gain is 3.11–4.80; loaded gain is 1.62–2.59. Worst drain/source
headroom exceeds 0.864 V. Maximum departure from the center's linear gain is
1.210 mV, and worst relative attenuation at 125 MHz is 0.261 dB.
Input-referred integrated noise is 31.0–54.2 µV RMS over
1 kHz–100 MHz, below the declared 100 µV bound. The checker independently
integrates the squared amplitude-density samples and compares the result with
ngspice's integrated-noise output within 2%. Amplitude density uses V/√Hz;
see the [ngspice noise-analysis documentation](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).
These results cover the selected circuit and operating range. ADC conversion,
complete PAM5 reception, mismatch and post-layout qualification remain separate.

Run `ctest --test-dir analog/build -R '^analog\.1g\.ihp_amplifier\.pvt$'
--output-on-failure`. Decks, raw DC/AC observations, source/model hashes and
individual bounds are retained in the test's results directory. The generic
amplifier independently passes native/Verilog-A electrical regressions under
`mos_amplifier` and `mos_amplifier_loaded`.

## Coupled receive front end

The [process receiver](receive_frontend.md) uses 2 pF sampling storage, a 1 kΩ
reference ladder, smaller sampling switches and buffered decision latches.
Closed conversion passes in both generic solvers and their comparison, including
32 consecutive conversions with sixteen-slice reuse. The previous 60 µm dummy
NMOS binding passed nominal PSP acquisition and one closed conversion, but
failed hot sixteen-slice acquisition and closed four-slice conversion. The
current 50 µm sizing candidate passes nominal one/four-slice acquisition and a
hot sixteen-slice diagnostic; its complete qualification remains open. See the
[receiver evidence and retained failure measurements](receive_frontend.md).
The [4 pF subclass](precision_receive_frontend.md) provides more hold charge
through the same ports when connected-circuit accuracy justifies its area.

The [wideband receiver](wideband_receive_frontend.md) reduces shared-amplifier
storage and comparator preamplifier resistance. It passes generic conversion
and nominal acquisition, but its full 32-conversion PSP run fails two codes by
one count beyond the allowed two-count error. The
[precision wideband variant](precise_wideband_receive_frontend.md) increases
sampling storage but fails the four-slice process settling/accuracy test. The
[parallel drive candidate](parallel_receive_frontend.md) instead adds an explicit
amplifier/buffer path with 2 pF storage and is being tested with the same limits. Neither result
establishes complete physical receive-chain qualification.

## Receive-buffer process experiment

The [derived precision receiver](precision_receive_frontend.md) increases each
hold capacitor while retaining the common amplifier/buffer/core-interface
hierarchy. Its native/Verilog-A and actual-PSP acquisition checks are under
qualification after the 2 pF process experiment exposed injection/feedthrough
errors that the generic channel law did not predict.

The [source-follower experiment](../../basic/nmos_source_follower.md) uses an
explicit 200 µm / 0.45 µm HV NMOS in 20 legal fingers, a grounded body and
500 Ω bias return. Its 90 independent PSP DC/transient checks pass across five
MOS corners, three temperatures, three supplies and two capacitive loads.
Worst 1.5 ns delivery error is 6.967 mV and settled error is 1.512 µV relative
to the corresponding DC sweep. DC gain ranges from 0.7396 to 0.8025. Body and
gate current enter the independent supply-KCL check. This standalone process
result does not qualify the connected amplifier/ADC bank or transistor layout.

## Digital module implementation

[`physical_module.py`](physical_module.py) consumes the exact mapped netlist
and current native/RTL/gate evidence. It implements the PHY-facing `ReceiveFilter`
or `ReceiveTimingLoop` with actual SG13G2 cells, a power grid and routed wires.
The test repeats the independent oracle on routed gates and compares its trace
byte for byte with native execution. It requires extracted slow/fast timing,
all PDK DRC groups including antenna/density, transistor LVS and every port
before promoting a named GDS. A missing or failing stage produces a failed
report and retains diagnostics. This implements standalone modules, not a die.

```bash
cmake --build analog/build --target one-g-timing-gds
cmake --build analog/build --target one-g-receive-filter-gds
```

The mapped evidence must exist first: run the corresponding `timing.rtl` or
`filter_datapath.rtl` CTest. The underlying `filter_pipeline.rtl` unit still checks
all 24 coefficients, but its 432 coefficient probe bits are not physical PHY pins.
`ReceiveFilter` exports only sample/control ports and uses a qualified
1100 µm square standalone layout;
the stopped 1600 µm debug-port attempt is retained as failed historical evidence.
The preceding unsigned bounded-magnitude weight products reduced the wrapper to
56,655 cells and 786,154.799 µm² mapped cell area, 15.83% below the preceding
934,007.761 µm² implementation. Native/RTL/frontend/IHP equality and the
slow-corner cell estimate (+1.83 ns at 8 ns) passed. Its 1400 µm routing run
timed out after three hours and has no physical pass. The new PAM5-code
implementation has 28,112 cells and 425,803.770 µm² of mapped cell area.
All seven reference/native/RTL/frontend/IHP/timing checks pass, with +1.97 ns
wrapper setup slack before routing. The smaller layout passes 37,724 routed
oracle vectors, strict transistor LVS and all 868 DRC categories, including
antenna, density and maximal checks. Routed setup/hold slack is +0.13/+0.05 ns
at 125 MHz. Fill coupling has not been extracted. See [mapped area](mapped-area.md)
for the retained qualification receipt and its scope.
Mapped cell area excludes routing and fill.
Each target creates a fresh retained directory below
`results/timing_runs` or `results/receive_filter_runs`. Direct script invocations
can use `--work` for a specific empty directory or `--work-root` for repeated
runs. Configure with `-DCPPHDL_1G_PHYSICAL_TESTS=ON` to register full module
GDS CTests; they depend on mapped-function fixtures and run serially. The placement
checkpoint and `placed_*` reports are estimates. Only the final `setup.rpt`,
`hold.rpt` and extracted SPEF establish the routed timing check. Pre-placement
fanout repair uses a 0.01 pF limit; subsequent physical repair uses 0.03 pF,
subject to more restrictive library pin limits, and still requires the 8 ns
timing target. New buffers undergo incremental global placement before timing
repair so legalization does not scatter them away from their loads.
Global-route parasitics then drive another slew/capacitance and timing repair,
with separate incremental-routing windows around legalization as in the
[OpenROAD reference flow](https://github.com/The-OpenROAD-Project/OpenROAD-flow-scripts/blob/master/flow/scripts/global_route.tcl).
The larger modules use 0.6 µm Metal2/3 fill spacing, above the PDK's 0.42 µm
minimum, to correct the measured density deficit. Remaining metal layers retain
1 µm spacing. DRC and extracted timing independently qualify each result.

Placement/CTS and routing run in separate OpenROAD processes through ODB/SDC
checkpoints. This avoids carrying placement-estimator dirty nets into global RC
repair. The final slew limit is explicitly **0.5 ns**, selected independently of
the tighter 0.3 ns sizing target. The slow IHP Liberty file's default limit is
2.5074 ns. `--max-transition` selects another interface budget; setup/hold and
the PDK/library limits still apply. The selected budgets are saved in the
report. Final fanout is 8. The timing controller's final capacitance budget is
0.05 pF; the larger filter uses 0.1 pF. Its measured clock branches reached
0.07 pF, with passing slew/fanout, so applying the smaller macro's 0.05 pF
budget unnecessarily rejected those branches. Both budgets are below the slow
library's default 0.3 pF, and individual pin limits still apply. In both modules,
the pre-placement fanout-4 and post-placement 0.03 pF settings are tighter
optimization targets. CTS uses clusters of at most four sinks and intermediate
branch buffers. The 400 µm timing-controller block uses 50 µm buffer spacing;
the 1600 µm filter uses 400 µm spacing with buffers at branch ends. Applying
50 µm spacing to the large block produced excessive insertion latency and a
−9.436 ns placement hold failure. Spacing is recorded per module. An earlier shallow tree drove sixteen
loads from its root and failed the IHP fanout-8 limit. A pre-route electrical
check rejects such failures before expensive detailed routing and DRC.
These final design budgets do not disable any more restrictive Liberty pin
limit. Every clock/data net remains subject to the final electrical checks.

The remaining implementation provenance is saved in the
physical report and final SDC. No slew limit is inferred from 1000BASE-T itself.

The 250 MHz ADC controller and normalizer request 0.2 ns setup sizing margin.
The normalizer retains its 0.05 pF total load budget and 30% capacitance sizing
margin. The ADC controller instead drives a 160 fF external load on each of
its 160 control outputs, with 0.18/0.22 pF preparation/optimization targets,
a 0.25 pF final total budget and 10% route capacitance margin. Its 0.5 ns slew,
fanout-eight, setup/hold and tighter Liberty pin checks remain enforced.
The original 5 fF output assumption did not include a real voltage translator.
The first loaded placement exposed a slew violation after hold legalization;
a final electrical repair pass now precedes the placement acceptance check.

[`input_charge.py`](input_charge.py) derives the translator load from all 150
passing PSP clock-interface cases. It verifies the original source, deck, log
and compressed waveform hashes, then integrates absolute input current over
complete edge-response windows. Returned charge counts toward the load.
The worst measured equivalent input capacitance is 140.525 fF; 160 fF is the
selected external-load budget. This is not extracted interconnect or a proof
that the translator drives every ADC control load. Reproduce it with:

```sh
python3 analog/1G/IHPSG13G2/input_charge.py \
  --proof analog/build/1G/IHPSG13G2/tests/results/fingered_clock_level_shifter_200ff_250_pvt/checks.json \
  --output analog/build/1G/IHPSG13G2/tests/results/clock_interface_input_charge/checks.json
```

Set `CPPHDL_1G_CLOCK_TRANSLATOR_PROOF` to that existing proof path to enable
the corresponding CTest; its guards always run. Prediction margins and sizing
repair follow OpenROAD's [resizer interface](https://openroad.readthedocs.io/en/latest/main/src/rsz/README.html).

Mapped inputs are copied and checked against their functional proof before
routing. Final provenance checks use those retained copies, so rebuilding the
original hardware-test directory cannot replace the consumed netlist or erase
its evidence. Live design/tool inputs and every retained copy must still match
their recorded hashes. A missing or changed copy fails verification.

`--resume-placement results/ORIGINAL --work results/FRESH` imports a stopped
run's `placed.odb` and `placed.sdc` only after checking the top, constraints,
mapped proof and saved design/PDK provenance. Original checkpoint hashes are
verified when present; adoption of an older interruption's unhashed checkpoint
is recorded explicitly. Routing, timing, routed-function checking, DRC and LVS
run again. A checkpoint is not a reusable passing physical result.

Builds, complete regression batches and memory-heavy EDA subprocesses share
`analog/out/eda-memory.lock`, including independent runner invocations.
Direct upstream jobs must use
`python3 analog/pdk/1G_repeater_IHPSG13G2/tool_process.py --exclusive -- COMMAND`
to participate. Wrap `cmake --build ...` and `ctest ...` at their entry points;
nested EDA runners inherit ownership so they do not deadlock against their
parent batch. Cell-only STA may explicitly use the bounded-memory path.
Both an earlier layout-extraction overlap and a later unwrapped C++ compilation
overlap ended with the large filter router receiving SIGKILL. Neither run
produced a passing routed result. The exclusion regression checks independent
families, nested tools and lock release without starting a physical tool.

`recheck_timing.py --implementation results/RUN --work results/FRESH_STA` runs
new slow/fast STA on that run's unchanged routed netlist and SPEF. It verifies
the original netlist/RC/GDS hashes, native/routed trace equality, current C++
source provenance and original timing-library hashes. Its report identifies the
exact mask and budgets. It neither reroutes nor rewrites the original physical
result, and does not claim a new DRC/LVS run. The early timing-controller layout
passes DRC/LVS but this recheck correctly rejects its fanout-16 clock root and
excessive clock-net capacitance. Library violations remain failures.

The [3 pF receiver subclass](balanced_receive_frontend.md) retains both input
drive paths and improves the four-slice process acquisition/accuracy margins.
Its one/four/sixteen-slice generic checks, separate waveform comparisons and sustained sixteen-slice nominal PSP conversion now pass; physical qualification remains open.
The transmitter slow and fast MOS corners both fail the unchanged 20 mV
weighted-plateau bound. See [the compliance boundary](../line-compliance.md)
for the distinct waveform requirements; these failures are retained.

[Programmable receive gain](programmable_receive_gain.md) passes all five nominal settings independently in C++RTL, Verilog-A and IHP PSP, with a separate generic waveform comparison. Receiver/AGC integration and physical qualification remain open.

The [buffered programmable receiver](programmable_receive_frontend.md) uses one
gain stage after the isolated differential input. The current candidate halves
sample storage to 1.5 pF and reduces sampler widths. Its single-slice closed
conversion passes independently in C++RTL, Verilog-A and IHP PSP; generic ADC
codes agree exactly. Complete sixteen-slice conversion and acquisition tests
are under qualification. Earlier 3 pF variants failed full-array PSP acquisition;
the receiver document retains those failures and the remaining integration
boundary. Generic waveform agreement alone does not qualify the receiver.

The inherited [receiver with higher gain-stage bandwidth](wideband_programmable_receive_frontend.md)
selects 0.25 pF compensation. All five gains pass independent single-slice
C++RTL and Verilog-A checks, and PSP checks at nominal, slow/hot and fast/cold
process conditions. Single-slice closed conversions also pass; full-bank checks remain
under qualification. The preceding variant fails slow/hot high-gain settling.

### Qualify a routed checkpoint

`physical_module.py --resume-routed-database path/to/core.odb` consumes an
existing OpenDB route in a fresh work directory. It still requires current
native/mapped evidence, checks the database's top and die, generates the module's
timing contract afresh, recomputes extracted RC/timing and repeats routed-gate
equivalence, fill, full PDK DRC and strict transistor LVS. It records hashes of
the consumed database and copied inputs. Previous timing or physical pass
reports are not imported. This supports qualification after a local routing
repair without repeating completed routing.

```sh
analog/pdk/IHPSG13G2/.cpphdl/venv/bin/python \
    analog/1G/IHPSG13G2/physical_module.py --module ReceiveFilter \
    --mapped-work analog/build/1G/tests/hardware/filter_datapath \
    --resume-routed-database path/to/core.odb \
    --work-root analog/1G/IHPSG13G2/results/filter_recheck_runs
```

Placement resume and routed-database resume are mutually exclusive. The latter
does not establish how the incoming route was produced; its hash identifies
the concrete implementation being independently checked. Passing this flow
qualifies the standalone module under its stated contract, not a full PHY chip.
