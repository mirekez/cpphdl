# Measured prototype results

Clean validation: **170/170 analog CTests passed**, including 121 labeled `10g`,
plus the root `cpphdl` Verilog-A CLI check. The complete analog run took 950 seconds
with CTest `-j 2` on the shared test host.

Validation environment: GCC 11, C++20 Release; OpenVAF 23.5.0 compatible build;
ngspice 47 with OSDI, 2026-10-17. See [test contracts and numerical settings](README.md)
and [SFI scope](../sfi.md). Values below come from independently checked native
and Verilog-A/SPICE trajectories, not measurements of fabricated hardware.

| Measurement | Native | Verilog-A / SPICE |
|---|---:|---:|
| Nominal complete-link checked serial / parallel errors | 0 / 0 | 0 / 0 |
| 80 Ohm loaded-link checked serial / parallel errors | 0 / 0 | 0 / 0 |
| Decoded complete 64-bit data payload errors | 0 | 0 |
| DFE enabled errors on strong postcursor ISI | 0 | 0 |
| Same channel with DFE bypassed | 49 | 49 |
| Long nominal CDR mean-period error | 6.75 ppm | 5.99 ppm |
| CTLE gain at 1 GHz | 0.8911 | 0.8908 |
| CTLE gain at 5.15625 GHz | 1.9657 | 1.9667 |
| Loaded / nominal TX RMS amplitude | 0.96860 | 0.96861 |
| Loaded / nominal RX RMS amplitude | 0.89448 | 0.89449 |

The nominal full-link maximum matched clock-edge difference is 0.02118 UI
(2.05 ps); halving integration steps reduces it to 0.01160 UI (1.12 ps). Nominal
TX/RX voltage RMS differences are 0.904/0.804 mV; refined differences are
0.441/0.498 mV. Both integrations recover the complete checked data block and all
four parallel history outputs. These are finite deterministic vectors.

During the 66 UI transition gap, LOCK falls to about 1 mV at UI 120 in both
solvers. After transitions resume at UI 146, LOCK remains above 1.15 V from UI 226
through the checked end window. All corresponding LOCK rise/fall events match
within 0.764 UI; steady LOCK RMS difference is 5.49 mV. Holdover phase is not
certified while the data has no transitions.

The complete generated 10G module catalog has 15 files. The parent catalog contains
51 basic/example Verilog-A files. Freshness and OpenVAF compilation are CTest
checks. Runtime traces, individual measurements and differences are reproducible
under `analog/build/10G/tests/results/`; they are ignored by Git. Test names and
commands, including both flows and the load/refinement checks, are in the test
README. Full SFI calibrated mask and BER compliance remains outside these results.
