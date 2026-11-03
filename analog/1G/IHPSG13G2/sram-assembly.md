# Resuming the embedded SRAM/control check

`resume_embedded_sram.py` reuses a completed native extraction instead of
repeating the hour-long geometry extraction. It verifies the preparation,
producer receipt and every recorded input/artifact hash. The retained extraction
and GDS remain unchanged. Use the same isolated KLayout Python runtime:

```sh
PYTHONPATH=out/klayout-lvs-0.30.12/python \
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/resume_embedded_sram.py \
    --retained build/embedded-sram-extraction \
    --output build/embedded-sram-comparison
```

The native database's layout/extraction and reference sections are copied byte
for byte, with a retained-prefix hash. Only the provisional comparison section
is reset. KLayout reads the native graphs directly; no SPICE conversion occurs.
The original malformed comparison record remains in the retained database.

Deep extraction can omit unused hierarchy ports while keeping their physically
labeled internal nets. For this assembly, all sixteen A data outputs and five
B data outputs are unused. `embedded_sram_ports.py` accepts promotion only when:

- All 32 SRAM data-output labels exist on the physical Metal2 label layer.
- The missing port has its own named extracted net and internal driver endpoint.
- Every reference use in all eight banks is a private, one-terminal stub, with
  no top port, device terminal or second instance connection.
- The promotion creates only a hierarchy pin and private parent stub; existing
  conductors and internal driver endpoints remain unchanged.

Used or shorted reference outputs, missing internal drivers, unexpected ports,
and missing physical labels fail. This normalization preserves flattened
electrical connectivity and the complete 223-port SRAM interface. It does not
repair a physical wire or create a device.

`embedded_sram_bootstrap.py` first establishes the 92 leaf-circuit mappings,
deferring the large SRAM and its enclosing circuit during this preliminary
step. Structural refinement then guides a newly constructed complete comparer.
Final comparison and independent graph audits include every circuit, device,
parent connection and required port. Preliminary exclusions cannot reach the
final comparer. A native crash or failed audit is recorded as failure by the
supervising process.

Only a completed `assembly_graph_correspondence_pass` permits the next command:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/finish_sram_assembly.py \
    --retained build/embedded-sram-extraction \
    --comparison build/embedded-sram-comparison/checks.json \
    --output build/embedded-sram-complete-drc
```

It reruns the full PDK DRC deck, including density, maximum rules and antenna,
and checks rule-table completion as well as violation counts. Both commands use
the shared EDA memory slot and bounded native processes. They produce new
receipts and preserve the original failed attempts. Generated reset copies are
reproducible and are removed after successful comparison.

The registered CTests cover native graph preservation, complete comparison after
leaf seeding, and unused-port promotion, with negative wiring/device/port cases:

```sh
ctest --test-dir build/behavior \
    -R 'ihp_sram.(lvs_comparison_reset|embedded_bootstrap|unused_output_ports)' \
    --output-on-failure
```

An assembly geometry pass covers this SRAM/control layout. Whole-ASIC timing,
electrical characterization and full-chip integration remain separate checks.

## Repairing row-end NWell corners

KLayout 0.30.12 reports 24 `NW.d` corners on the retained assembly that passed
with 0.30.5. The same unmodified maximum-rule deck reproduces the difference on
a small crop. `row_end_wells.py` pairs the opposing row ends and extends the
existing well edge by 80 nm, leaving 320 nm to the reported tie edge against
the 310 nm rule. No PDK rule is disabled or changed.

The generator requires a 1 nm grid and only the expected top-level `NW.d`
markers. It adds NWell drawing shapes only, rejects new coverage of protected
device masks, preserves connected-well count, and verifies all other masks,
instances and chip boundary. It writes a new GDS and provenance receipt.

```sh
PYTHONPATH=out/klayout-lvs-0.30.12/python \
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/row_end_wells.py \
    --source build/embedded-sram-extraction/fixture/assembly.gds \
    --violations build/embedded-sram-complete-drc/drc/assembly_FlowControlledRepeater_full.lyrdb \
    --output build/embedded-sram-repaired-wells

PYTHONPATH=out/klayout-lvs-0.30.12/python \
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/qualify_row_end_wells.py \
    --candidate build/embedded-sram-repaired-wells \
    --retained build/embedded-sram-extraction \
    --comparison build/embedded-sram-comparison/checks.json \
    --output build/embedded-sram-repaired-qualification
```

Qualification first runs full DRC, including maximum, density and antenna
checks. Only a pass permits fresh transistor extraction and the complete graph
comparison/audits. Original geometry and receipts stay intact. A frozen producer
receipt binds the new extraction separately from the evolving overall status.
The earlier LVS pass cannot qualify a changed mask. Added well area also needs
electrical assessment; even a geometry pass leaves whole-chip qualification false.

```sh
ctest --test-dir build/behavior -R 'ihp_sram.row_end_well' --output-on-failure
```
