# Receive timing confidence

`ReceiveTimingConfidence` tests measured timing-loop behavior before the PCS
idle qualifier may advertise receive readiness. It consumes the signed
14-bit detector error, Q24 period and correction-valid pulse already produced
by `ReceiveSampleRecovery`; source phase and source frequency are never inputs.
The initial 64-idle qualifier alone allowed legal peer transmission while the
receiver still had approximately 1426 ppm period error. Both earliest frames
were lost in `out/receive-earliest-peer/v2/decomposition.json`.

Each evidence window contains 1024 accepted correction observations. Invalid
clocks neither count nor change the result. The candidate requires:

- Absolute signed error sum at most 27487 over the whole window and each
  128-observation subwindow.
- Period change from first to last observation at most 537 Q24 counts.
- At least 128 nonzero detector observations.
- Total absolute detector error at most 2097152.

The actual resampler proportional gain is 40/Q24 UI per detector count. The
sum bound therefore limits net proportional correction to about 0.065536 UI
per window, equivalent to 64 ppm averaged over 1024 observations and 512 ppm
over each shorter subwindow. Short windows reject cancellation between long
opposite-slope excursions. The period bound is approximately 32.008 ppm of
measured movement; it is not a measurement of residual source frequency.
The excitation requirement rejects quiet or disconnected detector evidence.
The absolute-error bound (mean 2048, half the detector's 4096 clamp) rejects
large alternating errors that would pass a signed-average test. Quiet but
otherwise valid data may conservatively remain unqualified.

Every complete window replaces the confidence result. Fault immediately
suppresses ready and clears accumulated evidence at the next clock; reset
also clears it. Transport continuity and clipping feed this fault input in
the composition. PCS lock, accepted clean idle history, RX_ER and pipeline
fill remain separate required conditions. Confidence does not suppress raw
PCS errors, and it does not establish BER or physical aperture closure.

The standalone native/RTL proof records 28429 identical clock states. Twelve
window scenarios test excitation, saturated-error cancellation, exact mean
and period thresholds, poisoned pauses, partial-window reset and immediate
fault revocation. Receipt:
`out/receive-timing-confidence/unit-v1/qualification/checks.json`.

The isolated v5 two-peer diagnostic uses actual PMA control, transmitters and
two-register readiness crossings. Each endpoint transmits at its earliest
legal link event, with one clean SEND_N arm tick and no elapsed-training gate.
Both earliest 1526-byte frames pass in all thirty cases: relative source rates
−1900/−1500/0/+1500/+1900 ppm, common delays 0/0.105/0.9 UI, and zero or bounded
±2-count ADC noise. Samples are generated causally on fixed ADC grids. All
traces and consumed source hashes are retained in
`out/receive-earliest-peer/v5/checks.json`; independent IEEE transmit-symbol
replay is recorded separately in `audit.json`.

These are functional discrete-clock tests. The free-running asymmetric slave
clock is a stress stimulus, not an implemented recovered transmit clock. The
status crossing test proves its two-edge functional latency, not metastability
MTBF. A separate PCS clock, physical clock generation, CDC implementation and
connected ADC/SAR timing remain integration requirements.

The maintained `ReceiveSamplePCSQualified` composition v4 also passes all
796260 native/RTL clock states, including independently recomputed confidence
windows, idle evidence and raw PCS outputs. Its eighteen fixed-source cases
retain the original thirty-six frames and 28764 checked bytes. Receipt:
`out/receive-sample-pcs-qualified/v4/qualification/checks.json`. The previous
v3 run passed these electrical checks but is retained as a provenance failure:
the shared converter executable changed during the run. v4 instead uses the
immutable `out/receive-qualified-tools/cpphdl` snapshot, SHA256
`1b0d00bd275c2969f7a99c37a738c8594123f713d92c79d3d231e8de17303c91`.

The maintained earliest-peer regression now compares both receive paths in
native C++ and RTL over all thirty cases: 1962253 exact complete-clock states,
sixty frames and 91560 bytes. Both producer streams are captured directly into
lossless archives; identical streams share one verified archive. The receipt
is `out/receive-earliest-peer/maintained-v2/qualification/checks.json`, with
independent archive/state and IEEE source-stream audits alongside it. Every
source index, packed symbol, frame-enable and source byte equals the previously
independently replayed v5 stream (981134 symbols). The maintained timestamps
use seventeen digits, so their text differs from the earlier six-digit trace;
this changes logging precision only.

`analog.1g.receive_earliest_peer.rtl` runs the complete matrix. Its native CTest
runs the formerly failing −1500 ppm / 0.105 UI case with ±2-count noise and
two long frames. Both use the maintained receiver class and the actual PMA
handshake/transmitter as native testbench models. This is receiver RTL
qualification, not a claim that the whole peer fixture has been synthesized.
