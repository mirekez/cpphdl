# Physical sampling storage

`capacitor_layout.append_mim` creates the pinned IHP `cmim` PCell. It chooses
square dimensions from requested storage using the PDK's own area, perimeter
and linewidth calculation, then rounds upwards on the 10 nm parameter grid.
It checks that the resulting nominal capacitance meets the request within 1%.
Supported requests start at 10 fF. The rounded result must meet the PDK's
8 pF nominal maximum and maximum plate area; an exact 8 pF request is rejected
because the next upward grid point exceeds that maximum. Unsupported requests fail explicitly.

The capacitor's `PLUS` terminal is its contacted TopMetal1 plate. `MINUS` is
its Metal5 bottom plate. Both have real conductor geometry, pin shapes and
labels. The original PCell via array and dielectric marker remain present.
Labels do not connect the two plates. The independent LVS schematic specifies
the selected width and length and retains plate orientation.

| Requested storage | Square plate side | PCell nominal storage | Bounding footprint |
| --- | --- | --- | --- |
| 100 fF | 8.11 µm | 100.201 fF | 86.676 µm² |
| 0.25 pF | 12.85 µm | 0.25013 pF | 197.4025 µm² |
| 0.5 pF | 18.20 µm | 0.50032 pF | 376.3600 µm² |
| 0.75 pF | 22.30 µm | 0.75017 pF | 552.2500 µm² |
| 1 pF | 25.76 µm | 1.00026 pF | 726.8416 µm² |
| 1.5 pF | 31.56 µm | 1.50005 pF | 1,073.2176 µm² |
| 2 pF | 36.46 µm | 2.00093 pF | 1,418.276 µm² |
| 3 pF | 44.66 µm | 3.00026 pF | 2,103.1396 µm² |
| 4 pF | 51.58 µm | 4.00055 pF | 2,785.728 µm² |

For the programmable receiver candidate, changing 3 pF to 1.5 pF saves
1,029.922 µm² per capacitor, 16,478.752 µm² per sixteen-slice lane and
131,830.016 µm² (0.13183 mm²) summed across eight lanes. Both geometries pass
strict LVS and local DRC; the electrically connected candidate still requires
full-bank qualification. This is summed bounding footprint, not die savings.

The inherited wideband receiver reduces the five physical gain-stage
compensation capacitors from 11 pF total to 2.75 pF total. All four requested
sizes (0.25, 0.5, 0.75 and 1 pF) pass strict LVS and local DRC. Including
unselected branches, their summed bounding footprint falls from 7,760.8268 to
2,050.2566 µm² per lane. Across eight lanes this saves 45,684.5616 µm².
Together with the 3 pF-to-1.5 pF sampler change, the measured capacitor-footprint
difference is 177,514.5776 µm² (0.17751 mm²). This remains a component-area
comparison; receiver and chip qualification determine whether that saving is
usable in the final die.

Increasing sixteen sampling capacitors from 2 pF to 4 pF adds approximately
21,879 µm² of summed capacitor bounding footprint per lane, or 0.1750 mm² across
eight receive lanes. This excludes routing clearance and other circuitry; it
is not the required die enlargement. In particular, the plate regions count
towards the Metal5 and TopMetal1 global density maxima. Circuit placement,
surrounding metal and fill must be checked together. The two receiver variants must still
pass their electrical tests. A nominal capacitance calculation does not
establish process variation, mismatch, parasitic coupling or extracted settling.

The regression passes real PDK LVS and complete KLayout DRC tables for all nine
sizes, and requires a deliberately disconnected top plate to fail LVS. Local
geometry, pin and antenna checks must have zero violations. An isolated macro
has no active/poly/metal density fill and its small bounding box can exceed
the global Metal5 and TopMetal1 density maxima. Both minimum and maximum global density
failures are retained, and
the report explicitly does not claim full-chip DRC. The eventual assembled
chip must pass the complete density checks as well.

The initial isolated 100 fF check passes LVS but reports the expected global
Metal5 maximum, and larger sizes also exceed TopMetal1, along with missing density on other materials. Integration must
provide the required surrounding area and fill; the macro report keeps
`full_chip_drc` false. No local width, spacing, contact, pin or antenna rule is
excluded.

All nine sizes pass strict two-port LVS and local DRC, including pin and
antenna checks. Removing every physical MIM contact causes LVS to fail as
required. The complete rule tables ran for each undamaged layout.

CTest `analog.1g.ihp_capacitor.layout` passes in 210.96 s, including the
rounded-maximum rejection. Each invocation keeps its own fresh `run-*` directory
and report; `checks.json` at the work root points to the latest result. Tool
termination cleans up child processes, and the larger DRC timeout includes
workstation contention without accepting partial rule output.

From the repository root, after preparing the PDK and KLayout toolchain:

```sh
cmake -S analog -B analog/build -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_capacitor[.]layout$'
```

The test is registered when the prepared PDK Python environment and KLayout
executable are available. The latest report is
`analog/build/1G/IHPSG13G2/tests/results/capacitor_layout/checks.json`; its
`run_directory` contains the GDS, independent schematic, complete rule outputs
and retained per-run report.

## Parallel bias storage

[`capacitor_bank_layout.append_mim_bank`](capacitor_bank_layout.py) implements
larger storage with equal PCells of at most 5 pF each. Metal5 and TopMetal1
straps separately connect every bottom and top plate. The schematic explicitly
instantiates each capacitor, and the report preserves its quantized dimensions
and total nominal storage. This supports the receiver's 10 pF bias capacitors
without exceeding the single-PCell limit.

The 10 pF bank uses two 5.00143 pF PCells, with 10.00287 pF total nominal storage
and a 7,228.1088 µm² bounding footprint. The 20 pF bank uses four, totaling
20.00574 pF in 14,750.6176 µm². Both pass strict two-port LVS and local geometry,
pin and antenna DRC. A disconnected top-plate strap fails LVS even though all
unit capacitors and their labels remain present. Complete DRC retains the
isolated minimum-density and Metal5/TopMetal1 maximum-density failures; these
banks require chip integration and extracted electrical checks.

Run `analog.1g.ihp_capacitor_bank.layout` with CTest. The latest report is
`analog/build/1G/IHPSG13G2/tests/results/capacitor_bank_layout/checks.json`.
