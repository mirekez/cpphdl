# SRAM physical-width reference variant

## Upper-metal density fill candidate

Full unfilled-macro DRC completes all 37 main rule tables, maximum rules and
density checks. Its three violations are minimum density on Metal5,
TopMetal1 and TopMetal2; each has zero original coverage. `sram_fill.py`
adds isolated shapes only on their filler datatypes (67/22, 126/22, 134/22).
The Metal5 tiles are 1 × 1 µm with 0.5 µm spacing; the top-metal tiles use
the PDK's 5 × 10 µm pattern with 3.05 µm spacing. TRANS, no-fill, slit and
NoMetFiller exclusions are preserved. Existing upper-metal drawing or fill
causes rejection before mutation.

```sh
mkdir -p build/sram-fill
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/sram_fill.py \
    --source pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_sram/gds/RM_IHPSG13_2P_1024x16_c2_bm_bist.gds \
    --output build/sram-fill/macro.gds --report build/sram-fill/fill.json
ctest --test-dir build/behavior -R 'ihp_sram.fill_guards$' --output-on-failure
```

The current candidate measures 44.03% Metal5 and 44.19% on both top metals.
An independent region comparison confirms unchanged original geometry,
boundary and top labels. Its complete KLayout DRC passes with zero violations
across 868 categories, including all 37 main rule tables, density, antenna
and maximum rules. The receipt is
`build/clock-integration/extracted-repair/sram-upper-fill-v2-candidate/drc_checks.json`.
The compact hierarchical candidate preserves every physical layer and the top
labels of that DRC-clean geometry. Fresh extraction and complete native graph
correspondence pass in
`build/clock-integration/extracted-repair/sram-filled-lvs-v1-candidate/checks.json`.
The graph contains 39 circuit definitions and 221880 recursively instantiated
devices; all 223 top ports and all device connections are checked. The compact
GDS is about 629 KiB, with 178 cells. Full DRC of this hierarchy is queued;
post-fill parasitic extraction, electrical qualification and integration remain
required. Neither result qualifies the complete repeater.

`sram_lvs_compare.py` uses joint structural refinement to propose 25663 unique
top-net correspondences. A bounded search resolves the remaining 68 symmetric
tie/buffer instances. KLayout comparison uses depth 2 and branch complexity 20;
the independent endpoint audit remains mandatory after comparison. The native
database preserves extracted resistance values and device-class metadata that
the diagnostic SPICE round trip lost. Zero extracted metal resistance is an
error even if both compared graphs contain that placeholder.

`sram_lvs_audit.py` independently checks a reported mapping against every
simplified circuit, net, port, device terminal and child-instance connection.
It requires complete bijections and checks MOS L/W and metal-resistor R/L/W.
For geometry-defined resistors, the PDK reader leaves schematic R at zero;
the audit derives the expected resistance from the extraction deck's sheet
resistance and the matching L/W dimensions. It does not accept that placeholder
as a physical zero-ohm resistor.
Only the SRAM's two MOS and three metal-resistor models are accepted. MOS
source/drain and resistor terminal symmetry are allowed; gate and body
connections must match exactly. Diffusion parasitics remain a separate
electrical qualification requirement. Search hints alone cannot qualify a
candidate. The twenty-one audit regression cases pass through CTest with both
KLayout 0.30.5 and the isolated 0.30.12 candidate. The auditor reads actual
netlist endpoints independently of the comparison snapshot; changed snapshot
gates, child connections and port labels must fail. The audit
also passes on the real 32-bit array's 388 devices, 238 nets and 82 ports,
including 132 simplified metal resistors:

```sh
ctest --test-dir build/behavior -R 'ihp_sram.lvs_graph_audit$' --output-on-failure
```

## Reproducing extraction and correspondence

`prepare_sram_lvs.py` generates the explicit reference variant, preserves
physical hierarchy and shapes, aligns extraction labels, and copies the
unchanged foundry device rules into an isolated working directory. Only guarded
hierarchy preparation and search hints are added to the comparison deck.
Five preparation tests check every local shape/instance, top labels, foundry
rules, stale reference-input hashes and refusal to overwrite an existing output. Six correspondence tests
exercise repeated comparison and rejection of changed circuits. Together with
the 21 audit cases, these 32 cases are registered under `ihp_sram.lvs_*` in CTest.

From `analog/`, with the PDK and repeater toolchain prepared:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/prepare_sram_lvs_tools.py
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/sram_lvs_flow.py \
    --fill --output build/sram-lvs-qualified
ctest --test-dir build/behavior -R 'ihp_sram.lvs_' --output-on-failure
```

The tool installer keeps KLayout 0.30.12 and its Python wheel under `out/`;
it verifies the binary package SHA256 and does not replace the PDK venv's
KLayout package. `--verify-only` checks an existing isolated installation.
Use a fresh output directory for each flow run. Heavy stages share the EDA
memory lock and retain input/artifact hashes. `qualify_sram_lvs.py` requires
the native database hash from the completed extraction receipt and rechecks
all upstream inputs. A passing graph result leaves `full_sram_qualified` and
`full_chip_qualified` false. The consolidated flow's regeneration replay is
queued behind receiver routing and the compact-layout DRC run.

## Reference circuit

`sram_reference.py` creates an explicit schematic variant for
`RM_IHPSG13_2P_1024x16_c2_bm_bist`. It preserves the supplied GDS geometry and
all circuit connections. It changes these schematic resistor dimensions:

| Resistor family | Original CDL width | Variant width | Length |
| --- | ---: | ---: | ---: |
| Bitcell Metal2 and Metal3 | 0.26 µm | 0.20 µm | 0.60 µm |
| Peripheral Metal3 | 0.26 µm | 0.24 µm | 0.60 µm |
| Peripheral Metal2 | 0.26 µm | 0.26 µm | 0.60 µm |
| Metal1 dummy | 0.26 µm | 0.26 µm | 0.60 µm |

The variant widths come from the actual layout. This is a reference-circuit
revision, not equivalence to the original PDK schematic. The tool does not
modify the PDK, widen LVS tolerances, discard devices, or qualify the macro.
It is not enabled in the full-chip flow.

The pinned extraction deck uses a nominal sheet resistance of 0.088 Ω/square
for Metal2 and Metal3. Thus a 0.60 µm body changes from approximately 0.203 Ω
at 0.26 µm width to 0.264 Ω at 0.20 µm width. The electrical sweep measures
the effect of this circuit change; matching geometry alone is insufficient.

The generator checks the complete layout's resistor inventory and metal
coverage, resolves every reachable subcircuit, and requires the expected
16384 bitcells, 64 Metal2 peripheral resistors, 192 Metal3 peripheral resistors
and 28 dummy delay cells. Indented subcircuit instances are significant and
are retained. Missing, recursive or ambiguous hierarchy fails generation.
Legacy `lvsres` names are resolved to the corresponding physical metal models.
Transistor dimensions and resistor terminals remain unchanged.

From the analog directory:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/sram_reference.py \
    --pdk pdk/IHPSG13G2 \
    --output build/sram-reference/reference.cir \
    --report build/sram-reference/reference.json
ctest --test-dir build/behavior -R 'ihp_sram.reference_' --output-on-failure
ctest --test-dir build/behavior -R 'ihp_sram.bitcell_sensitivity' --output-on-failure
```

The report records input hashes, measured dimensions and the circuit revision.
`reference_generated` means generation succeeded; it does not mean LVS or
timing passed.

The October 31 isolated 32-bit array check passes strict LVS with the 0.20 µm
bitcell variant: 82 ports and 388 simplified devices. A separate 90-case PSP
sensitivity experiment covers TT/SS/FF/SF/FS, −40/27/125°C, 1.08/1.20/1.32 V
and both write polarities. Both references pass the independent bitcell write,
read and retention checks. Maximum waveform difference is 0.392 mV, minimum
sampled differential read margin is 0.365 V, and maximum measured write latency
is 87.5 ps under that fixture's conditions.

Those experiments use schematic transistor diffusion, ideal peripheral drivers,
100 fF line loads and nominal metal sheet resistance. They do not qualify
complete SRAM timing, extracted interconnect or peripheral behavior. The
complete macro must pass DRC/LVS and its required electrical/timing checks
before integration. Diagnostic receipts are under
`build/clock-integration/extracted-repair/sram-layout-width-reference-v2/`,
`sram-resistor-sensitivity-pvt/`, and `sram-full-physical-width-diagnostic/`.
