# Physical receive-lane controller

`adc_lane_physical.py` implements `ADCReceiveLaneController<13>` in IHP
SG13G2 standard cells. It includes startup sequencing, the SAR bank,
sample normalization and slice tags. The smaller layout described in
[the raw controller flow](adc_bank_physical.md) excludes startup and
normalization; its area must not be reported as this complete controller's
area.

The input is a passing native, converted RTL and mapped-gate regression.
Preparation validates source and artifact hashes, checks all public port
widths, regenerates cell simulation models from the pinned Liberty and
repeats all 15,468 clocks through the mapped gates. Truncated trajectories,
changed interfaces and stale evidence are rejected before placement.

The physical flow places cells, builds the clock tree, routes, extracts
interconnect RC and checks setup/hold timing and electrical limits. It then
replays the same trajectory through routed gates, streams the actual PDK
cell layouts, adds fill, and runs full KLayout DRC and transistor LVS.
Every stage must pass before `component_checks_pass` is recorded.

The initial floorplan is 500 × 500 µm, with a 4 ns controller clock. Each
public trial, acquire and convert output, plus `clear_out` and
`evaluate_enable_out`, has a 0.16 pF interface load. This conservatively
loads all 162 exported control bits, including constant compatibility
positions. There are thirteen connected comparator inputs. The three
remaining comparison bits have no receiver gates and therefore require no
input-gate antenna diode. The routing check requires thirteen protected
receiver inputs; it does not count unused pins as devices.

From the repository root, after building the mapped lane regression:

```sh
python3 analog/1G/IHPSG13G2/adc_lane_physical.py \
    --proof analog/build/1G/tests/hardware/adc_receive_lane/checks.json \
    --pdk analog/pdk/IHPSG13G2 \
    --tools analog/out/repeater-toolchain \
    --verilator /path/to/verilator \
    --cxx /path/to/clang++ \
    --work analog/build/1G/IHPSG13G2/tests/results/adc_receive_lane_physical
```

`--prepare-only` checks mapping without starting layout. `--die-size`
selects a candidate floorplan; a smaller value still requires every check.
Each invocation preserves its own run directory and reports.
`--resume-routed-database /path/to/core.odb` reuses a routed checkpoint when
correcting a verification setting. It still repeats RC extraction, constrained
timing, exact gate replay, fill, full DRC and LVS in a fresh run directory.
It cannot be combined with `--resume-placement`.

The first routed 500 µm candidate passed the complete gate trajectory but
failed extracted setup timing by 0.06 ns on `valid_out`; hold slack was
+0.05 ns. That layout is retained as failed evidence. The revised lane
drives startup acquisition directly and enables its conversion bank only
when ready, removing redundant precharge qualification from the valid path.
Its complete native trace is byte-identical to the earlier trace, and fresh
RTL and mapped-gate regressions pass. A new physical run uses the same
floorplan, 4 ns period, 0.1 ns uncertainty, 0.4 ns I/O delays and loads.
That run initially could not legalize two local antenna diodes. The legalizer
now searches a wider region while preserving the timed logic placement and
the original 25 µm maximum gate-to-diode distance. All thirteen diodes then
legalize; the largest achieved distance is 21.36 µm. The completed reroute passed exact gate replay but failed setup by 0.09 ns
on `acquire_out`, where the high-fanout ready signal added buffering. That
failed run is retained as well.

The current lane directly composes startup, SAR bank and normalizer, keeping
ready on their synchronous reset controls and enable on the physical output
gates. The native trace remains byte-identical, and RTL/mapped checks pass.
That route passed exact replay and hold (+0.04 ns), but missed setup by
0.04 ns inside the normalizer's partial-product addition. The startup output
paths no longer limit timing. The updated normalizer splits that addition
into an eight-bit sum and parallel upper-word corrections without adding
latency. Its 409,477-clock unit proof and the unchanged full lane trace pass
in native, RTL and mapped cells. A fresh physical run uses the same floorplan
and timing/load constraints; no false-path exception or relaxed clock budget
is introduced. That run again passed exact gate replay, with hold slack
+0.053446 ns, but setup failed by 0.173278 ns from the normalizer's integral
sign bit to its high-clipping register. The current revision decodes byte
carry and upper-word sign/range directly. Native, RTL and mapped unit/lane
regressions pass, together with the native boundary regression. The new
500 µm route passes extracted setup at +0.077566847 ns and hold at
+0.053977489 ns, and repeats the complete gate trace exactly. The timing checker accepts 5,918 extracted signal nets. Full KLayout DRC
passes all 868 categories with zero violations. The default LVS run reports
matching circuits, but its simplification removes three disconnected
compatibility inputs from both netlists, so the strict interface check
rejects it. Disabling simplification also prevents standard-cell matching.

The verification adapter now checks these three input bits against the
mapped graph and both extracted/schematic top nets before simplification.
Missing nets, public-port aliases, device loads and child-pin loads are
rejected. Only the proven disconnected pins are restored afterward; active
transistor comparison and the full port contract remain unchanged. On the
same filled GDS, strict LVS then passes all 238 ports, 45 circuits and 67,292
instantiated devices. The original failed reports are retained.

This adapter is integrated into the physical flow. The fresh checkpoint run
repeats extraction, timing, exact replay, fill, DRC and LVS and now records
`component_checks_pass`. It retains the same setup/hold margins, all 868 DRC
categories with zero violations, and strict LVS across 238 ports, 45 circuits
and 67,292 instantiated devices. Its acceptance receipt covers the current
verification sources under
`build/1G/IHPSG13G2/tests/results/adc_receive_lane_clipping_verified_ports_500/run-jmqpou5y/physical/`.
The filled digital module occupies 500 × 500 µm, or 0.25 mm².


Timing reports now retain nine fractional digits. The checker explicitly
rejects negative slack rounded to `-0.00`, and regression cases cover both
setup and hold. Timing-only rechecks also preserve all 162 loaded analog
control ports, including clear and comparator evaluation enables.

CTest exposes `analog.1g.ihp_adc_lane.physical_guards` and
`analog.1g.ihp_adc_lane.physical`. The physical test requires the mapped-lane
fixture and shares the heavy-hardware resource lock. The native/RTL/mapped
regression checks 2,914 delivered samples, startup, request gaps, calibration
settings, tags and flushes.

This flow qualifies a digital component. Analog converters, physical
voltage translators, supply/clock qualification, padframe and chip
interconnect require their own connected verification. Post-fill coupling
extraction remains outstanding. The 500 µm module passes its standalone
physical contract; its uniform I/O constraints do not qualify the connected
analog return path.

The [connected timing budget](receive_timing_budget.md) also requires the
trial-to-comparator and comparator-to-controller paths to fit their actual
half-cycles. Standalone 4 ns timing does not establish those analog apertures.
