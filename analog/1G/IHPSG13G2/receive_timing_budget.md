# Connected receiver timing

The receiver uses a 4 ns controller period. `ADCSARController` changes the
trial code on one edge and consumes the latched comparison at the next such
edge. The analog comparator evaluates on the intervening opposite edge.
The closed conversion fixture currently supplies that evaluation waveform
and the control drivers from simulation.

A passing routed controller and a passing isolated DAC do not yet establish
this connected schedule. The controller's 0.4 ns I/O constraints describe its
standalone boundary. The DAC unit checks threshold accuracy throughout
1.5–2.0 ns after each command, with a 100 ps source edge and 50 ohm source
impedance. These two allowances cannot simply be added to a half-period
without accounting for the actual clock and control paths.

For each slice, use the local controller launching edge as the time origin.
Let `skew` be the evaluation-clock arrival relative to its ideal opposite
edge, measured against that same local launching clock. The connected
implementation must establish both inequalities:

```text
trial clock-to-Q + control routing + translation/DAC settling
    + preamplifier settling + comparator input margin <= 2 ns + skew

comparator regeneration + decision translation + return routing
    + controller setup margin <= 2 ns - skew
```

These are budgets to verify, not measured component delays. Settling stages
interact electrically; the final check must observe the connected circuit
under actual trial transitions, comparator loading and finite supplies.
Static timing supplies the digital path bounds. Transient simulation must
then check the resulting earliest/latest control and evaluation arrivals,
including the falling edge and duty-cycle uncertainty. Common clock-tree
insertion delay cancels only where the actual shared distribution proves it.
Reset, clear and startup release must use the same physical clock boundary.

Sampling has a separate aperture check. A physical TRACK translator must
drive the transmission-gate and dummy-device gate charge. Report its achieved
opening time and sampling error against the declared nominal aperture.
Retargeting the expected voltage to a delayed opening can conceal aperture
error. An intentional phase advance belongs in the clock design and requires
its own timing proof; changing an ideal source's edge does not implement it.
All thirteen slice phases must retain the acquisition, hold and code limits
after integration.

The next integration therefore requires the actual trial/track/evaluation
drivers, their shared bias and clock distribution, the comparator return
interface, and the routed controller's path delays. The current ideal-driver
conversion receipts and standalone timing reports retain their unit scope.

`receive_interface_timing.py` reads a completed routed lane implementation and
its original load/clock constraints. It reports minimum/maximum rising/falling
paths for each control output and comparator-return input. Output reports
retain both root-clock arrival and, for register-launched paths, the delay
from the local launching clock pin. Combinational paths from external enable
or reset inputs are identified without inventing a local launching register.
The three unused slice positions must have no timing paths. Their physical
indices are derived from the generated array adapter: unused trial words
occupy packed bits 0–23, while unused scalar controls occupy indices 13–15.
The report preserves a failed implementation status when used diagnostically. The report keeps
its source, tool and input hashes and leaves the implementation unchanged.
It also records boundary slew and the original Liberty measurement thresholds.
Propagated output slew is distinguished from the configured input slew: the
latter is an assumption of the standalone controller contract. The pinned
library measures slew from 20% to 80%, with delay arrivals at 50%. A full
linear PWL edge therefore lasts `slew / 0.6`; its midpoint, rather than its
start, aligns with the reported arrival. This linear equivalent is a stimulus
construction aid, not the cell's actual voltage waveform or a crosstalk bound.

```sh
python3 analog/1G/IHPSG13G2/receive_interface_timing.py \
    --implementation /path/to/passing/lane/physical \
    --pdk analog/pdk/IHPSG13G2 \
    --tools analog/out/repeater-toolchain \
    --work /path/to/new/interface-report
```

The script uses OpenSTA's per-port rising/falling path selection and expanded
clock reporting; see its [command definitions](https://github.com/The-OpenROAD-Project/OpenSTA/blob/master/search/Search.tcl).
These are worst constrained path reports, not a claim that all external
analog delays fit. The [connected decision experiment](core_receive_decision.md)
is the next electrical check, with an explicit driven-input boundary.

## Proposed shared sampling-clock experiment

A shared translated aperture event could avoid replicating a full translator
on every slice. This is a proposal, not a qualified clock circuit. Keep each
slice's acquisition state locally: set it when acquisition starts, then reset
it only when that slice's pre-established selection admits the shared aperture
event. The state must remain closed until the next acquisition. Selection must
be stable before and after the event at the actual high-voltage gate; its
translation, routing and setup/hold requirements belong in the timing proof.

Do not implement `TRACK_i = select_i && periodic_clock`. The present SAR keeps
TRACK high for two complete controller periods, with overlapping acquisitions
on adjacent slices. Periodic AND gating would interrupt that interval and can
reopen a held sample. A local set/reset implementation also needs defined
behavior for simultaneous requests, reset, enable changes and startup's
all-slices-tracking precharge. A transistor-level implementation and its
hazard behavior remain to be designed.

The smallest useful connected experiment includes one shared translator,
physical distribution loaded by all thirteen local gates, and three adjacent
active slices. Exercise overlapping acquisition, rotating selection, selection
arrival extremes, no unintended reopening during conversion, startup precharge
and release, disable/reset, and both clock polarities. Include the actual
transmission-gate and dummy-device charge, finite supplies and shared bias;
check low-voltage device exposure as well as acquisition, hold and code error.

The rejected direct translator diagnostics measured roughly 0.67 ns opening
delay and substantial complementary-output skew. Sharing the translator does
not remove this latency or make its two outputs simultaneous. The experiment
must declare a physical phase plan derived from the single reference clock,
then measure the opening aperture against that plan over process, supply,
temperature and load. Retargeting the expected voltage to a measured late edge
is not a timing solution. If a phase advance is required, its circuit and
uncertainty must be included; no phase-generation mechanism has been selected.
Compare the resulting local state, gating and distribution area with separate
translators only after those circuits and loads are explicit. The current
ideal-driver conversion qualification remains unchanged and does not cover
this proposed clock path.

Evaluation-clock distribution must also share or explicitly match the routed
controller's launch-clock distribution. The current routed lane reports about
1.55 ns root-to-launch-leaf insertion delay. An analog evaluation edge taken
directly from the root clock would not inherit that delay and could arrive
before the routed trial path has settled. Keep root-referenced and local-leaf-
referenced arrival reports separate; cancel insertion delay only for paths
whose actual common distribution supports that cancellation. The connected
unit fixture's command at 0 ns and evaluation at 2 ns define its driven-input
boundary, not a routed timing result. The eventual phase plan must satisfy
both decision half-cycle budgets and the sampling aperture; it must not be
chosen merely to fit an achieved held voltage.

Comparator-return constraints must ultimately use connected minimum/maximum
arrival times over electrical corners, referenced to the root clock and
actual capture-clock tree. The standalone uniform 0.4 ns input assumption
can induce hold-repair delay chains; those chains are neither a measured
analog return delay nor an additional universal 0.4 ns allowance. First
extract the actual per-port return paths, then derive integration constraints
from the connected loop while checking setup and hold across the same cycle.
Do not remove repair cells or revise constraints before that evidence exists.

## Measured routed boundary

The 500 µm clipping-decoder candidate produces all 712 interface reports.
At the slow cell corner, local register-to-trial delays range from 0.658 to
0.888 ns; root-clock trial arrivals range from 2.209 to 2.590 ns. Those are
digital boundary delays, before the connected ladder and preamplifier.
After complete physical acceptance, the fresh interface report retains all
712 paths and includes slew. The largest trial slew among those selected
paths is 0.220393 ns, equivalent to a 0.367321 ns full linear edge. The
largest clear slew is 0.223646 ns, equivalent to 0.372744 ns. These exceed
the connected decision unit's 100 ps full stimulus edge. Because these are
worst-slack path reports, their observed slews are not automatically the
maximum over every possible path. The integrated stimulus must use justified
arrival/slew bounds, with the declared Liberty thresholds and the achieved
control loading, rather than reuse the ideal unit edge.

Its existing uniform 0.4 ns input constraint also caused long hold-repair
chains on comparator returns. The worst reported return has 3.291 ns of
internal delay and a 5.388 ns required arrival at the destination register,
so it requires the comparator input by about 2.097 ns after the root edge.
This is incompatible with simply appending the standalone comparator test's
late return to this routed controller. The repair contains four delay cells.
The next connected implementation must derive minimum and maximum input
arrival constraints from the actual analog return and shared clock path,
then reroute and recheck setup and hold. Removing buffers or relaxing a
constraint without that electrical evidence cannot establish operation.

The same routed candidate's comparator input nets have 5.064–18.142 fF of
nominal extracted wire capacitance. SPEF declares `PIN_CAP NONE`; adding the
actual first delay-cell and antenna-cell A-pin capacitances gives up to
20.793 fF, or 21.565 fF using the largest characterized pin capacitances
across the slow/fast Liberty files. The connected decision unit uses 20 fF
and therefore does not cover this worst boundary. Analog-to-controller
wiring and fill coupling are additional loads. A subsequent integrated
fixture must use the actual returned net or a justified conservative load,
and repeat its timing/electrical checks after the hold-buffer reroute.

`receive_interface_loads.py` makes this measurement reproducible from the
completed physical receipt, its SPEF and its original Liberty files. It
separates grounded and coupled wire capacitance, then adds every connected
input pin. The pinned OpenRCX format declares `PIN_CAP NONE`; pin-inclusive
extraction is rejected to avoid counting those loads twice. The tool checks
the capacitance sum against each extracted net and requires all thirteen
active returns. It reports a second switching scenario with twice the coupled
capacitance, corresponding to simultaneous opposite equal-voltage swings.
For this route, that scenario reaches **32.453 fF**, including the largest
characterized pin loads. This is a conditional effective load, not a universal
bound on crosstalk, differently sized voltage swings or timing. A connected
transient should retain the coupled RC network and its aggressor waveforms
when a single capacitance cannot represent them.

```sh
python3 analog/1G/IHPSG13G2/receive_interface_loads.py \
    --implementation /path/to/completed/lane/physical \
    --pdk analog/pdk/IHPSG13G2 \
    --output /path/to/new/return-loads.json
```
