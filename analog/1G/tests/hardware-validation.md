# Hardware and MOS-bias validation, 2026-10-18

This extends the historical electrical-system validation; it is not a complete
Clause 40 PHY compliance result.

The new PMA startup composition and framer readiness output are under validation.
Historical receiver/framer mapped counts and timing below precede that added
output; they must be regenerated before qualifying the current composition.
See [startup integration](../pma_control.md) for the native results and remaining
RTL/mapped checks.

After the capacitor-loop startup fix, all **290/290** analog CTests selected by
`ctest --test-dir analog/build -L analog -LE 'hardware|physical'` pass in
2005.40 s. This covers native/Verilog-A circuit comparisons across basic ICs,
examples, 10G and 1G, including the new loaded/unloaded bridge and MOS amplifier.
It excludes heavy digital mapping and physical layout flows, which have their
own evidence below. The log is `analog/out/analog-capacitor-regression.log`.

The last complete combined regression passed **134/134 CTests** in 1254.81 s:
127 tests under `1G/` and seven under `pdk/`, including the routed-core
DRC/LVS/timing flow. This precedes the new fixed-point filter tests below;
it is not evidence that the current full-chip target passes.
Run `ctest --test-dir analog/build -R '^analog\.(1g\.|pdk\.)'
--output-on-failure -j 2` to repeat this selection with the prepared tools.
The log is retained locally in `analog/out/1g-resumed-regression.log`.
After reducing Viterbi survivor selection to the four legal predecessors,
all eight focused native/RTL/mapped/fault/catalog tests passed again in
676.60 s (`analog/out/1g-survivor-ctest.log`). Six later catalog, runner,
CDL translation, padframe and verification-guard CTests pass in 24.42 s.
Padframe assembly and failure-reporting tests are distinct from the standalone
full padframe DRC/LVS run, which failed as recorded below.

| Block | Independent checks | Native / RTL / IHP gates |
| --- | --- | --- |
| ReceiveClockAligner | Nine phase/rate cases; 360 frames; 287,640 bytes; FCS, error bits and minimum IFG; short frames; overflow; stopped receive clock; reset | Pass / pass / pass; exact output traces |
| PCSSymbolMapper | 8,816 normal/control/sign vectors; invalid controls and reset | Pass / pass / pass; exact output traces |
| PCSSideStream | 20,000 master/slave bit-array recurrence steps; enable holds; invalid zero seed; reset | Pass / pass / pass; exact output traces |
| PCSTransmitBits / PCSTransmitter | 82,099 independent GF(2)/control vectors; all 14 conditions, both roles, basis states, errors and re-arm | Pass / pass / pass; exact output traces |
| PCSBranchMetrics | 16,706 exhaustive-oracle/exact/random/extreme/tie/sign cases | Pass / pass / pass; exact output traces |
| PCSTrellisDecoder | 17,670 states against full-path dynamic programming; 4096 clean bytes; repeated circular-metric wraps; noise benefit; terminated and back-to-back tails | Pass / pass / pass; exact output traces |
| PipelinedPCSBranchMetrics | 16,709 clock states against exhaustive constellation search, bubbles and reset | Pass / pass / pass; exact output traces; +1.68 ns cell setup slack |
| PCSReceiveSynchronizer | 90,243 vectors; both roles, 16 pair polarities, unseeded acquisition, seed changes, stuck input and PMA loss | Pass / pass / pass; exact output traces |
| PCSReceiveFramer | 3730 independent tag/control/sign vectors, malformed SSD and PMA failure | Pass / pass / pass; exact output traces |
| PCSReceiver | 64 complete 64/1518-byte frames, 51,136 octets, 12 error frames; another 4244 fault/recovery vectors | Pass / pass / pass; exact output traces |
| PipelinedReceiveFilter | 37,724 transaction states, all 24 weights, stalls/flush/reset and independently scored noisy holdout | Pass / pass / pass; exact output traces, including the synthesis frontend |
| ReceiveTimingLoop | 16,513 integer states and 12 closed-loop noisy PAM5 cases of 20,000 symbols each, ±100/500/1500 ppm | Pass / pass / pass; exact output traces, including delayed feedback |

Mapped designs contain only actual `sg13g2_*` cells: 4005 cells for the receive
aligner, 471 for the symbol mapper and 216 for the side-stream generator.
The transmitter uses 874 cells, single-stage branch metrics 9503,
pipelined branch metrics 20,636, and current trellis 23,052.
The synchronizer uses 431 and framer 432. The current 39-clock composed receiver
uses 49,748 cells and passes the independent native/RTL/IHP-gate checks with
+0.94 ns slow-corner cell setup slack at 8 ns. Seven focused receiver/decoder
CTests pass in 1108.90 s (`analog/out/receiver-quartet-ctest.log`). Their
cell counts, netlists and traces are in
`analog/build/1G/tests/hardware/BLOCK/hardware_checks.json`. These mapped-gate
runs establish functional synthesis equivalence, not routed timing or CDC signoff.

The current decoder's quartet selection removes one serial best-state
comparison level. Its independent native/RTL/IHP contract passes again, and
slow-corner cell setup slack is +1.51 ns at 8 ns. The earlier three-level
selection missed composed-receiver cell setup by 0.22 ns; that failed report is
not a timing qualification for the revised design.

The SAR electrical integration independently converges and passes both solvers;
its six registered CTests pass in 492.82 s. Four PAM-range conversions and four
center-level conversions return identical codes across flows, with at most one
count of error against the ideal settled
quantizer. Direct latch-to-ladder connection failed; adding a transistor
preamplifier limits the measured held-input error to 2.76 mV. The compensated
sample/hold separately passes all 54 actual-PSP PVT/load cases. See
[front-end circuits](../frontend.md) and [ADC feedback](../adc.md) for the exact
boundaries; the full link still uses its older simulation ADC.

`link_pcs` carries actual transmit-PCS Ethernet frames through the coupled
electrical system. Each native/Verilog-A flow independently recovers two frames,
144 octets and 384 scored GMII words with no errors, then the recovered streams
match exactly. The original floating-point filter's worst MSE is
0.00170894/0.00164434 for native/Verilog-A respectively. The new actual fixed-point
hardware class also independently recovers both frames in each flow: worst MSE
0.00138252/0.00135413 and minimum cancellation improvement 26.00/26.18 dB.
Both see the same declared training boundary; no target updates weights in holdout.

The fixed-point class also passes the nominal, loaded, noisy and refined
electrical cases in each flow, with exact recovered symbols; disabling adaptation
exposes 282 holdout errors in each solver. Its unit oracle checks 5059 states,
including every coefficient. The separately pipelined implementation matches
37,724 native/RTL/frontend/mapped transaction states and recovers all 4078
noisy-channel holdout symbols. Four focused CTests for the current six/ten-cycle
pipeline pass in 598.58 s, including slow-corner cell timing with +1.78 ns setup
slack at 125 MHz after buffering. The earlier five/nine-cycle variant missed
placement setup by 0.11 ns. The current version adds a registered partial weight
product; it still needs a passing routed timing result. The single-cycle reference also
passes exact mapped-gate checks, but fails preliminary 125 MHz timing
with −66.65 ns setup slack; it must not be used as a timing-qualified PHY.
See [filter arithmetic and scope](../receive_filter.md).

`ReceiveTimingLoop` has six cycles of feedback latency. All four CTests pass in
77.47 s, including native/RTL/mapped closed-loop trajectories and slow-corner
cell timing (+2.41 ns setup slack). Its 12 noisy PAM5 cases have zero scored
symbol errors and mean phase error below 0.0121 UI. Physical implementation
uses the same independent oracle again after routing; its results are distinct
from these cell-only timing estimates. See [timing](../timing.md).

`biased_driver` and `trimmed_driver` each pass native electrical simulation,
generated Verilog-A/ngspice, the common absolute checker and cross-solver
comparison. `generated` and `generated_compile` also pass after adding both
exports to the catalog. Existing `mos_driver` native/Verilog-A/comparison,
three original IHP-driver process cases and algorithm regression pass.

Actual PSP simulation of fixed MOS bias passes TT. Six-bit switched-resistor
bias passes TT/SS/FF with externally supplied codes 30/42/20 respectively;
all use the same amplitude, headroom, supply-current and 2 ns settling bounds.
See [the full bias contract](../IHPSG13G2/biased_driver.md), including the
untrimmed SS/FF failures, prototype voltage scale and temperature/calibration
limitations. These process-corner tests are separate physical-model checks;
they do not claim identical equations between generic MOS and PSP.

The [padframe and full-layout verification](../../pdk/1G_repeater_IHPSG13G2/chip.md)
have their own evidence. Assembly and failure-reporting tests pass; the padframe
now has an actual PDK seal ring, fill and a connected 32-cell core decoupling
row. The row matches its transistor schematic and clears the earlier nine
antenna violations. Full serial DRC passes with zero violations across 869
categories, all 37 main tables, maximum rules, antenna and nine density
materials. The earlier incomplete parallel run was rejected. I/O transistor
LVS mismatches remain unresolved.
A complete PHY, matched chip
schematic/layout, qualified padframe and full-chip extracted signoff remain open.
