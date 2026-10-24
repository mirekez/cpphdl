# Physical implementation of the compact SAR controller

`adc_bank_physical.py` starts from the independently checked thirteen-slice
mapping. It validates source and artifact hashes, recreates simulation cells
from the pinned IHP Liberty, and repeats the native/mapped trajectory check.
`physical_module.py` then places and routes the controller, extracts RC, checks
setup/hold timing and electrical limits, and repeats the complete functional
trajectory through the routed gates. Only then does it stream actual cell
GDS, add legal fill, run full KLayout DRC and perform strict transistor LVS.

The recorded 400 × 400 µm result passes these component checks: 8,256 clocks
match exactly, setup/hold slack is +1.24/+0.08 ns, and all 868 DRC categories
pass. Its 0.16 mm² area includes the raw SAR controller's routing and power
network. It excludes analog conversion cells, the calibration/startup logic,
voltage translators, clock generation, pads and full-chip interconnect.
Post-fill coupling is not extracted.

The corrected 360 × 360 µm result passes the same complete checks: exact
8,256-clock trajectories, +1.10/+0.06 ns extracted setup/hold slack, all
868 DRC categories and strict LVS (39 matched circuits, 163 ports and
34,832 devices). Its 0.1296 mm² layout is 19% smaller than the 400 µm result. The initial run passed
functional comparison, timing and LVS but failed Metal2/3 global density and
two Metal3 minimum-width markers at one routing corner. Those failures are
retained; they are not waived. `repair_route_corner.py` changes a selected
interior right-angle DEF bend from explicit zero extension to the normal
half-width extension. It rejects ambiguous coordinates, endpoints, vias,
straight segments and unexpected extensions, and records input/output/script
hashes. The reviewed correction at `trial_out[24]`, DBU coordinate
`352800 170100`, adds exactly 0.01 µm² of Metal3 and removes the two local
width markers. The corrected fill policy uses the pinned patterns and legal
remainder fill. The accepted corrected database has passed fresh extraction, complete
functional checking, full DRC and strict LVS. Metal2/3/4 global densities
are 36.45/36.99/37.49%. Post-fill coupling remains unextracted.

A fresh DEF import can reverse a Verilog bus declaration while keeping each
named physical bit unchanged. The post-route simulation adapter reconstructs
the public array interface according to the imported declaration. It records
any permutation, rejects changed index sets and unsupported permutations,
and is checked by the full native/post-route trajectory comparison. The
physical netlist and geometry keep their actual bit names.

For a fresh run, build the mapped bank proof first, then invoke from the
repository root with your prepared tool paths:

```sh
python3 analog/1G/IHPSG13G2/adc_bank_physical.py \
    --proof analog/build/1G/tests/hardware/adc_bank_mapped/checks.json \
    --pdk analog/pdk/IHPSG13G2 \
    --tools analog/out/repeater-toolchain \
    --verilator /path/to/verilator \
    --cxx /path/to/clang++ \
    --work analog/build/1G/IHPSG13G2/tests/results/compact_adc_bank_physical
```

`--die-size 360` requests the smaller candidate. `--prepare-only` performs the
fresh mapping recheck without placement. The generic physical runner accepts
`--resume-routed-database` to requalify a corrected checkpoint in a fresh work
directory. Previous reports are not imported as passing evidence. Source
snapshots, tool/PDK hashes, the final layout and all check reports stay in the
selected work directory. See [qualification status](qualification-status.md)
for the accepted boundary.
