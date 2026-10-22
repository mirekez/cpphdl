# Hardware and MOS-bias validation, 2026-10-17

This extends the historical electrical-system validation; it is not a complete
Clause 40 PHY compliance result.

The final combined regression passed **110/110 CTests** in 535.27 s: 103 tests
under `1G/` and seven under `pdk/`, including the routed-core DRC/LVS/timing flow.
Run `ctest --test-dir analog/build -R '^analog\.(1g\.|pdk\.)'
--output-on-failure -j 2` to repeat this selection with the prepared tools.
The log is retained locally in `analog/out/1g-final-ctest.log`.
Padframe assembly and failure-reporting tests are distinct from the standalone
full padframe DRC/LVS run, which failed as recorded below.

| Block | Independent checks | Native / RTL / IHP gates |
| --- | --- | --- |
| ReceiveClockAligner | Nine phase/rate cases; 360 frames; 287,640 bytes; FCS, error bits and minimum IFG; short frames; overflow; stopped receive clock; reset | Pass / pass / pass; exact output traces |
| PCSSymbolMapper | 8,816 normal/control/sign vectors; invalid controls and reset | Pass / pass / pass; exact output traces |
| PCSSideStream | 20,000 master/slave bit-array recurrence steps; enable holds; invalid zero seed; reset | Pass / pass / pass; exact output traces |

Mapped designs contain only actual `sg13g2_*` cells: 4005 cells for the receive
aligner, 471 for the symbol mapper and 216 for the side-stream generator. Their
cell counts, netlists and traces are in
`analog/build/1G/tests/hardware/BLOCK/hardware_checks.json`. These mapped-gate
runs establish functional synthesis equivalence, not routed timing or CDC signoff.

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
has 22 DRC violations and unresolved transistor LVS mismatches in I/O/ESD/substrate
circuits. A complete PHY, matched chip
schematic/layout, qualified padframe and full-chip extracted signoff remain open.
