# Reproducible cell-area measurement

[`mapped_area.py`](mapped_area.py) measures a flattened IHP netlist after exact
native/mapped functional comparison. It verifies the netlist and source hashes,
recounts every physical master and rejects unknown cells, changed inventories
or failed/stale functional evidence.

```sh
python3 analog/1G/IHPSG13G2/mapped_area.py \
  --work analog/build/1G/tests/hardware/filter_datapath \
  --liberty analog/pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib \
  --lef analog/pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_stdcell.lef \
  --output analog/build/1G/tests/hardware/filter_datapath/area.json
```

The PAM5-code filter wrapper has 28,112 mapped cells. Liberty area totals
425,803.770 µm²; actual LEF placement footprints total 425,823.350 µm².
The preceding compact filter had 56,655 cells and 786,154.799 µm² of Liberty
area. The new mapped area is 45.84% smaller. The report preserves
both values and identifies cell-level differences in the supplied library.
It records all input hashes and each cell type's count and area contribution.

These totals exclude later physical repair, placement whitespace, routing,
fill, analog blocks and pads. The standalone 1100 × 1100 µm filter passes the
physical checks below after the earlier 1400 µm run timed out.
Full-chip die area must come from verified
integration, including those other costs.

The corrected 1100 µm routed candidate has 37,180 ordinary core cells and 33
antenna cells, totaling 540,135.994 µm² of placement footprints. A further 73,122
spacer/decoupling cells occupy 577,592.467 µm². The die contract is 1,210,000 µm²
(1.21 mm²). These are different quantities from mapped logic area; the larger
functional footprint includes clock-tree and electrical/timing repair. The
candidate passes routed-gate equivalence and extracted timing/electrical limits.
Main, density and antenna DRC now report zero violations after legal remainder
fill. Metal2/3/4 density is respectively 36.032%, 35.515% and 37.666%. The
preceding fill missed Metal4 density at 34.822%; its failed report is retained.
The combined DRC process reached its 7200 s wall limit during the maximal-rule
run. A separate execution completed that missing group against the identical
retained GDS. The combined report now passes all 868 categories, with all 37
main tables, maximum rules, density and antenna checks complete. Strict
transistor LVS passes: 49 matched circuits, all 39 top ports and 394,192
instantiated devices. Fresh source/artifact, routed-trajectory, timing, DRC and
LVS checks produce a named `ReceiveFilter.gds` through
[`complete_qualification.py`](complete_qualification.py). The latest local
receipt is `results/receive_filter_completed_runs/checks.json`.

Setup slack is +0.13 ns and hold slack +0.05 ns at 125 MHz. These use nominal
routed-wire RC with slow/fast IHP cells. Fill coupling is not extracted; full-chip
post-fill timing, power integrity and integration remain separate obligations.

Active/Poly fill runs hierarchically to bound memory. Its generated masks have
zero XOR against the original flat fill on this routed filter, and all original
cell geometry and instances are preserved. This changes how fill is computed,
without changing the fill masks or relaxing DRC.

Read the inventory from a concrete routed database with the prepared OpenROAD:

```sh
REPEATER_DATABASE=path/to/core.odb \
REPEATER_AREA_OUTPUT=path/to/routed-cell-area.tsv \
    analog/out/repeater-toolchain/bin/openroad -no_init -exit \
    analog/1G/IHPSG13G2/routed_area.tcl
```

The TSV separates cells by their actual LEF master class and footprint. Keep
`CORE_SPACER` separate when comparing functional logic, and use die area when
sizing the integrated chip.

`analog.1g.ihp_area.evidence_guards` tests failed/stale functionality, changed
netlists, incorrect cell counts, unknown masters and invalid library dimensions.
