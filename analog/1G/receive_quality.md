# Receive readiness from observed symbols

`ReceiveQuality` establishes a local receive-quality indication from accepted
PCS output words. It first discards the known PCS pipeline latency after reset
or any quality loss, then requires 64 consecutive clean idle words while PCS is
locked. The observation length matches the existing synchronizer's 64-symbol
qualification scale; it is an engineering startup-health criterion, not a BER
measurement or a claim about arbitrary channels.

Only accepted PCS word pulses advance either counter. Invalid clocks hold the
counters, and data before qualification interrupts the consecutive-idle count.
Once ready, clean data preserves readiness. A valid RX_ER, lock loss, transport
fault, clipping or disable revokes readiness combinationally and clears the
stored evidence on the next clock. Reset also flushes both counters. Reacquisition
requires a fresh pipeline flush and fresh idle evidence. The raw PCS word and
valid outputs remain visible: startup idle RX_ER is not discarded or converted
into a successful idle.

`ReceiveSamplePCSQualified` composes the fixed-sample interpolator/timing loop,
PCS, `ReceiveTimingConfidence` and this qualifier in a 250 MHz domain.
The idle qualifier is enabled only while measured timing confidence is true;
a confidence loss revokes readiness and flushes its idle evidence. PMA availability comes from the
input stream from startup, with no 34,000-clock training gate. The source still
contains the same declared idle training and the same two independently encoded
Ethernet frames; neither source phase nor frame position is adapted to the DUT.
Every data-valid frame byte must arrive with hardware readiness and without
RX_ER. An independent trace model checks counter, immediate-revocation and
pipeline-fill behavior on every clock, and compares data to source truth at the
fixed 39-accepted-symbol PCS latency.

The unit regression exercises adjacent valid ticks, poisoned invalid pauses,
data before readiness, valid errors, lock loss, transport faults, clipping,
disable, reset and revocation during pipeline fill. It compares native and
RTL state at the same settled pre-edge observation point.

The initial no-flush counter proof is retained under `out/receive-quality/v1`;
its criterion counted clean output words but did not account explicitly for
reset-filled pipeline words. The trained sample/PCS proof remains under
`out/receive-sample-pcs/v2`; immediate-start pilot traces and startup errors are
retained separately. These records are not overwritten by the new qualifier.

The existing `PMAEndpoint.receiver_quality_in` is the intended logical consumer:
it combines local quality, continuity and PCS lock for both the local PMA
readiness handshake and transmitted receiver status. `PMAControl` still requires
peer readiness and its own state dwell. This unit does not force link up.
The current endpoint/control runs in the 125 MHz symbol domain, where its
125-count dwell means 1 µs. It must not simply be reclocked at 250 MHz.
A synchronized readiness crossing with explicit fault-drop behavior remains
required before connecting this 250 MHz candidate to that endpoint. Adaptation,
physical sampling/decision timing, channel noise/echo and full link startup
remain separate qualification gates.

The original pipeline-flush/64-idle criterion alone is insufficient for earliest
peer transmission. The retained `out/receive-earliest-peer/v2` failure loses
both earliest legal frames while short PCS lock intervals still allow ready.
The maintained composition therefore adds the measured detector/period evidence
described in [receive_timing_confidence.md](receive_timing_confidence.md).
Its criterion uses accepted timing observations, not an elapsed training delay.
The old v2 qualified fixture remains a valid trained-frame proof with historical
source hashes; it does not establish earliest-link safety. The v5 two-peer
diagnostic separately passes sixty earliest legal long frames across thirty
rate/phase/noise cases and independently replays 981134 IEEE transmit symbols.
