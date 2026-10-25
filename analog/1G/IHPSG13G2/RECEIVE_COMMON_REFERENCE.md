# Shared common-mode reference candidate

`lead_receive_common_reference` supplies eight finite-source MDI input networks.
The main receiver still selects its previously qualified passive network. This
candidate passes fresh native, Verilog-A, and three PSP corner qualifications.

The unchanged nine-transistor cascode OTA drives a **150 µm / 0.45 µm** HV
NMOS shunt sink with **15 fingers**. A **1000 Ω + 2 pF** series branch from
DRIVE to OUT compensates the loop. The original 20 fF DRIVE–OUT coupling,
80 fF DRIVE storage, 0.25 pF OTA compensation, input capacitors, 1000 Ω pullup,
and diode bias device remain present. The diode feed stays 2500 Ω; no increased
bias current is used. The reference contains 11 MOS devices.

The physical target divider is **10 kΩ above / 12.15 kΩ below**, giving
**VDD × 243/443**, or **1.810158 V at 3.3 V**. This target is declared in the
circuit, runner, and strict process binding. It is never inferred from OUT.
The buffer owns **80 pF** of output storage; the bench adds **16 pF**, giving
**96 pF total**. Integration must implement all 96 pF. Every capacitor starts
at zero charge. The supply ramps through 0.25 Ω with 20 pF supply storage.

Each of eight lanes has two 25 Ω source impedances, 500 Ω series resistors,
150 Ω bias resistors, 100 Ω differential termination, and two 150 fF sensed-node
capacitors. All source means change together every 8 ns with 100 ps edges and
superimposed differential symbols. Measurement covers 256 ns after 16 µs of
cold startup with a 2 ps maximum measurement step. Every solver state counts.

## Absolute contract and retained corner results

The limits remain **15 mV settled error**, **100 mV transient error**,
**2 mV divider error**, and **1.80–2.00 V sensed input common mode**. Only the
explicit target ratio changes. `receive_common_target.py` reuses the original
contract for all electrical, loading, excitation, completeness, and RC checks,
and evaluates the three target-dependent errors against the declared ratio.
Its tests prove exact original-contract parity at 6/11 and rejection at the
unchanged bounds. `receive_common_stimulus.py` reconstructs every imposed
source through its finite 25 Ω/500 Ω terminal KCL relation and verifies the
complete 32-symbol/100 ps schedule.

| Fresh PSP qualification | Settled error | Transient error | Input common-mode range |
| --- | ---: | ---: | ---: |
| TT, 27 °C | 7.569 mV | 74.423 mV | 1.816234–1.988997 V |
| SS, 125 °C | 4.388 mV | 89.368 mV | 1.804613–1.996397 V |
| FF, −40 °C | 11.957 mV | 64.715 mV | 1.823775–1.986153 V |

All checks pass, including source schedule, exact connected RC response, supply,
drive, and 6–20 mA injected loading. Worst margins are 3.043 mV settled,
10.632 mV transient, and 3.603 mV input range. These are nominal resistor and
capacitor values with MOS process/temperature corners, not mismatch or
extracted-interconnect qualification.

The freshly exported maintained TT PSP deck is byte-identical to its passing
diagnostic (SHA-256 `bb3f6aef5035e0115c3344457c3b1dd545287cc5c20c956346979379780d27b7`).
An independent graph audit checks all 132 components and 65 nodes, including
MOS pin roles/fingers, source schedules, and analysis conditions. Fresh PSP runs reproduce those three diagnostic decks and measured results
exactly. Native and Verilog-A also pass: settled errors are 2.595 / 2.591 mV,
transient errors 42.687 / 42.761 mV, and input ranges 1.840914–1.979818 /
1.840855–1.979849 V. Their full-waveform maximum difference is **0.574 mV**
at DRIVE, below the unchanged **5 mV** limit. Every fresh source hash was
rechecked after qualification.

## Selection evidence and preserved failures

The previous 200 µm sink, 250 Ω compensation, 84 pF storage and 6/11 target
passed all nominal flows but failed SS125 settling/input minimum and FF−40
settling. Its source snapshot and receipts remain under
`out/lead-reference-200um-84pf-source-snapshot` and
`build/1G/IHPSG13G2/tests/results/lead_receive_common_reference*`.

Reducing the sink to 150 µm restored low-load OTA headroom; increasing the
series compensation resistor to 1000 Ω suppressed settled ringing. At 84 pF,
SS still reached only 1.793010 V at the sensed inputs. Doubling bias increased
power by about 8 mW but did not fix that input limit, so it was rejected.

A physical divider shift was then compared with additional storage at SS125.
Keeping 1.800 V and increasing total storage to 108 pF passed with only
0.276 mV minimum input margin. The declared 1.810158 V target with 96 pF
passed with 3.603 mV margin, saving 12 pF. The selected change does not add
transistors. A setpoint-only projection had less than 1 mV margin and was not
accepted as physical proof. All failed/interrupted diagnostics and original
6/11 proofs remain retained; no bounds were relaxed.

## Reproduction and guards

The bench and runner are `tests/lead_receive_common_reference.{cpp,py}`.
`lead_receive_common_process.py` checks the reviewed 11 MOS, 66 resistor,
37 capacitor inventory, zero initial charge, sink sizing, physical target
ratio/topology, feedback, storage and compensation connections before legal
finger sizing. The two 1000 Ω resistors are distinguished by their connections.

`tests/lead_receive_common_process_test.py --bench <compiled-bench> --pdk <pdk>`
checks the actual export and twelve rejected mutations: missing/changed RC
branches, sink strength, cold charge, wrong wiring, and target ratio/topology.
`tests/receive_common_target_test.py` checks explicit target handling alongside
the original contract and source-profile guard suites.

Fresh receipts and export audit are under
`build/1G/IHPSG13G2/tests/results/lead_receive_common_centered96/`.
Retained corner diagnostics are in `common_corner_candidate/*comparison_centered96`;
the head-to-head SS comparison is in `common_setpoint_analysis/storage_comparison_ss.json`
under the same results directory. Historical diagnostic checker sources are
preserved in `out/common-centered96-diagnostic-source-snapshot`.

This bench does not qualify the amplifier, ADC, hybrid, line driver, pad/ESD,
or complete PHY. The reference is not yet integrated into the receiver.
