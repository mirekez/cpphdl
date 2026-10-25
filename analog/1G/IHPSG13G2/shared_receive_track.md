# Independent shared TRACK experiment

This is an unqualified circuit candidate. It does not replace the balanced
sampler bank or establish receiver timing closure. The first retained export,
static topology results and native diagnostic are under
`out/shared-track-unit/`.

`shared_receive_track.hpp` connects seventeen actual core-to-high-voltage
translators: thirteen pre-established closing selections and shared strobe,
enable, precharge and opening permission. Each of thirteen local cells contains
static CMOS request gates and a retained NAND latch. Its output drives the
selected balanced sampler, including the local complement inverter, main and
dummy transistor gates, coupling capacitors and cold 1.5 pF storage. The shared
translator has doubled output and latch channel strengths; the remaining
translators use their original profile. Shared bias storage is 25 pF.

The first native revision failed sequencing. The translated strobe's first
half-rail falling edge arrived at 2.594 ns after the command began. The next
opening permission crossed half rail at 2.202 ns, and unintended TRACK edges
appeared at 2.548 and 2.562 ns. This short overlap was enough to retain the next
requests during the preceding event. Twenty-five of 32 stable event checks
failed; worst fixed-target error was 162.662 mV. The failed trace, checks and
machine-readable edge decomposition remain under `out/shared-track-unit/native`;
source snapshots are `source-v1` and `source-v1-extra` beside that directory.

Revision 2 adds a physical shared pulse shaper: a 1 kΩ / 1 pF delayed input,
an inverter, NAND gate and sized output inverter. It adds eight MOS devices
and five explicit capacitors, for 655 MOS devices and 689 capacitors total.
The output inverter binds to N40/P80 µm, with legal four/eight-finger geometry.
The source pulse width, input waveform and nominal aperture remain unchanged.
The same full schedule runs under `out/shared-track-unit/v2`; pulse width,
reset before the next selection, and every requested/unrequested TRACK edge
are checked separately from fixed-aperture error. This revision is not yet
electrically qualified, including across process corners or intrinsic fanout.

The initial export contains 647 MOS devices and 684 explicit capacitors. These
counts describe the complete unit, including sampling switches; they are not a
layout area estimate. There is no lumped replacement for the thirteen actual
clock loads. The unit excludes the receiver DAC and preamplifier and therefore
cannot qualify their charge injection into held samples.

For ordinary events, selection `i` closes slice `i` and requests opening of
slice `(i+2)%13`. Opening permission suppresses only that new opening. Closing
continues through sampling gaps. This preserves two full 4 ns acquisition
periods. A periodic AND of a shared clock and slice selection would truncate
that interval and is not used. A conflicting open/close request gives closing
priority through the pre-established complementary closing selection.

Disable resets every local latch; enabled precharge sets every latch. The
fixture explicitly clears the precharged state before ordinary events. Its
32-event schedule includes two adjacent disabled openings, simultaneous
requests, global disable with acquisitions outstanding and a later restart.
Selection commands precede strobe commands by 2 ns. All imposed core edges
are finite 400 ps ramps with 50 ohm sources. Cold supply ramps and finite
supply resistance/storage remain present.

The fixed nominal aperture is the half-rail time of the commanded strobe,
not the achieved TRACK transition. The checker reports actual closing delay
and held error relative to that fixed target, preserving a 20 mV error limit.
Sequencing and aperture checks have separate outcomes. A future phase advance
would require an explicitly declared physical clock distribution and timing
proof; changing the target to match the resulting held value is not acceptable.
The unit neither models routed root-to-leaf clock insertion nor establishes a
matched evaluation-clock distribution.

Static evidence is deliberately limited. `shared_receive_track_topology.py`
checks 1,040 truth-table cases against the exported transistor connectivity,
including absence of floating or contending Boolean outputs. The wiring audit
checks all translator, retention-loop and sampler connections. These checks
cannot establish analog settling, switching hazards, intrinsic process charge,
low-voltage exposure across corners or the closed receiver timing loop.

Revision 2 native results are retained under `out/shared-track-unit/v2/native`.
All 32 complete edge audits pass, including no early ordinary edges, no
reopening, overlapping acquisitions, opening gaps, close priority and global
disable. Every shared pulse rises and falls once; half-rail widths are
662–758 ps, and the event is low before the next selection command. The
low-voltage observation limit passes. This is generic-model evidence; process
intrinsic gate loading still requires the separate PSP diagnostic.

The original stable-state receipt retains one checker failure at event 22.
Its observation window (91.2–91.8 ns after warmup) follows the explicit disable
command at 90 ns. The waveform first reaches the requested mask 384 at
89.034 ns, then correctly clears both live slices at 90.554 ns. The complete
edge audit accounts for that asynchronous disable and passes. The original
receipt has not been rewritten; `native/decomposition.json` records this
window defect, observed states and waveform hash. A future maintained checker
must derive observation intervals from the explicit global-control schedule.

The fixed aperture still fails: actual closing half-rail crossings lag the
commanded half-rail aperture by 834–842 ps, and maximum held error is
78.313 mV against the unchanged 20 mV limit. Thus the pulse shaper fixes
request sequencing but is not a drop-in sampling-clock solution. The PSP run
is a delay/fanout characterization with that same fixed target, not promotion
of the candidate or evidence that its aperture is acceptable.

## Proposed physical feedback experiment (not implemented)

Reuse `1G/reference_clock.hpp`'s physical PFD, switched resistive charge paths,
20 pF lead/lag filter, 3 pF smoothing and controlled ring. Its existing generic
qualification establishes multiplication, not IHP process timing. The current
feedback observes the unloaded oscillator before clock distribution; moving
that observation after a representative sampling path can make the existing
loop compensate delivery latency without a second oscillator or an imposed
phase advance. This requires a new composition with explicit feedback ports,
not assignments through child internals.

The feedback replica would be driven from the same loaded launch leaf as the
thirteen actual request cells. Include the physical pulse gate, reset request,
local retained state, complement inverter and balanced sampler gate loads.
Reopen the replica during the inactive interval and close it on each event;
verify that this faster repetition represents the real slices' closing delay.
Observe both TRACK falling and CLOCK_N rising. A gate edge is a phase sensor,
not proof of the effective held-voltage aperture: all original capture checks
remain necessary across signal level, slope and charge injection.

The smallest new prerequisite is a physical edge-receiver pair. Attenuate the
replica's high-voltage clock with finite resistors into a core CMOS buffer,
and match its Thevenin resistance, explicit storage and buffering in the
reference arm. An illustrative nominal 7 kΩ/4 kΩ divider scales 3.3 V to
1.2 V and has 2.545 kΩ Thevenin resistance; those are starting values for a
new diagnostic, not selected geometry or an exposure proof. Its approximately
0.3 mA high-state loading acts on the replica and must be represented when
comparing it to actual sampler loading. All supply combinations, resistor
variation, intrinsic capacitance, input slew and core terminal stresses must
be checked. The existing `level_down` is another realizable sensor, but its
large asymmetric delay would also need physical reference-arm compensation.

Feed the sensed 250 MHz edge to the existing MOS divide-by-two. In the
reference arm, a matching DFF captures one on each 125 MHz rising edge and
clears during the reference low interval; this reproduces a clock-to-Q path
rather than silently ignoring the feedback divider's latency. Match output
loading and inspect clear-release margin. The two 125 MHz outputs drive the
existing PFD. Its UP/DOWN switches physically charge the filter and change
ring current. Reference/feedback receiver and clock-to-Q mismatches remain
measured phase errors; matching topology alone does not certify cancellation.
Test edge association explicitly because the existing PFD can associate
across a cycle following unfavorable reset release.

First qualify the new receiver/DFF arms open-loop with imposed 125/250 MHz
clocks, finite edges, actual replica loads, cold startup, early/late/missing
feedback and reset release. Report reference-to-PFD and sampler-to-PFD delays,
terminal exposure and signed mismatch over process, supply, temperature and
load. Then close the reused analog loop and require fundamental frequency,
bounded control/headroom, no cycle slips, original reference-relative capture
apertures and all thirteen local states. Neither test may define its target
from the achieved sampling edge.

Evaluation needs an independently proven half-cycle relationship to the same
launch distribution. The existing three-stage ring does not prove equal high
and low durations. A candidate 500 MHz ring followed by physical divide-by-two
could provide a 250 MHz clock with opposite edges separated by one oscillator
period; it adds one existing 62-MOS DFF but requires a fresh tuning-range,
startup and process qualification. It does not remove clock-to-Q mismatch or
routing skew. Include the measured controller tree and actual evaluation path
in the closed-loop cycle check before selecting either that architecture or
an explicit delay-control path. No new clock topology is selected by this note.

## Architecture review: absolute reference phase is not yet justified

Read-only review of `1G/receive_timing.hpp`, `1G/tests/timing.cpp`,
`1G/pcs_receiver.hpp` and `1G/receive_alignment.hpp` does not establish a
requirement that the ADC aperture coincide with an external-reference edge.
The timing loop estimates phase from data; the PCS expects recovered symbol
samples; the aligner crosses recovered receive timing to the board reference
while allowing latency variation. The [IEEE master/slave interpretation](https://www.ieee802.org/3/interp/interp-2-0705.pdf)
requires local-source timing for the master and data-recovered loop timing for
the slave. It does not supply a prescribed ADC aperture phase.

A common, stable delivery delay may therefore be tolerable once represented
in a declared delivered-clock boundary and a working receive timing loop.
That has not been demonstrated: current timing tests interpolate the source
waveform in host code at arbitrary predicted times, and the physical actuator
is not connected to the ADC array or timing controller. A phase-feedback PLL
should not be added solely to make the old ideal-driver timestamp pass.
The edge-sensor unit remains useful characterization; closing its replica
feedback loop is deferred pending this architectural requirement.

The next integration proof must consume the actual uniformly sampled ADC
stream, include its fixed conversion/filter latency, perform real resampling
or clock-phase actuation, and recover source symbols with source truth used
only for scoring. Exercise different common clock phases, frequency error,
startup, missing clocks and restart. Preserve sample ordering and cadence,
measure slice-dependent skew and time-varying error separately, and verify
sampling closure before the first physical SAR evaluation. Static phase
recovery does not cancel random jitter, periodic slice skew or insufficient
analog settling. The retained 78.313 mV fixed-aperture failure is unchanged;
a new system boundary needs new end-to-end evidence, not a rewritten receipt.

The completed v2 PSP TT/27°C diagnostic retains the original command aperture:
`out/shared-track-unit/v2/psp_tt/checks.json`. All thirteen loaded TRACK edge
sequences pass the independent no-extra-edge/no-reopening audit. Startup and
low-voltage exposure pass (maximum 1.416498 V). Closing arrives
1.322828–1.334000 ns after the declared aperture; maximum held-target error is
111.213918 mV, so the unchanged 20 mV aperture contract fails.

The process pulse also fails the declared selection setup condition. EVENT
must be below 20% of VDD from +2 ns, when the next selection can begin. Its first
pulse stays above that threshold until +2.190353 ns; later examples cross at
+2.047412 and +2.042353 ns. Correct observed state transitions do not waive this
overlap. The original event-22 state-window failure remains in the receipt;
the separate edge audit correctly accounts for the asynchronous disable.
`out/shared-track-unit/v2/psp_audit.json` records every pulse tail and verifies
all consumed source hashes and the complete waveform archive hash.

The fixed-sample resampler and PCS composition now have separate native/RTL
functional proofs in `receive_sample_recovery.md` and `receive_sample_pcs.md`.
Those establish recovery from fixed-grid samples with declared common delays;
they do not qualify this physical pulse tail, slice skew, or first SAR setup.
