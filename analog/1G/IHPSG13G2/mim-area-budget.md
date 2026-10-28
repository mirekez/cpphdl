# Whole-chip MIM area

The PDK enables **MIM.gR** with its recommended checks. Its current
`sg13g2_tech_default.json` specifies 174800 µm² per chip on MIM drawing
layer 36/0. Passing this recommendation is separate from mandatory geometry,
density, LVS and electrical checks.

`mim_area_budget.py` reads that limit from the installed PDK and measures
hierarchical GDS using KLayout. It includes instance arrays and merges
overlapping drawing shapes. It records input hashes, measured top cells and
copy counts. It does not generate or certify a layout.

For planning, supply an inventory such as:

```json
{
  "scope": "component_subtotal",
  "components": [
    {"name": "transmitter", "gds": "transmitter.gds", "copies": 8}
  ]
}
```

Paths are relative to the inventory file. A `top` field selects a specific
cell; layouts with multiple top cells require it. Components must represent
distinct physical instances: do not count both an assembled parent and its
children. For a final chip, use `"scope": "chip_layout"`, one assembled GDS
and `"copies": 1`. An incomplete component subtotal can establish an excess;
a subtotal below the limit leaves the rest of the chip unaccounted for.

From `analog/`:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/mim_area_budget.py \
    --pdk pdk/IHPSG13G2 --inventory build/mim-inventory.json \
    --output build/mim-budget.json
ctest --test-dir build/regeneration-audit \
    -R '^analog[.]1g[.]ihp_area[.]mim_budget$' --output-on-failure
```

An exceeded recommendation returns exit code 1. The report always sets
`drc_signoff` to false; run the PDK DRC on the assembled chip as well.

The October 28 component inventory measured 63310.9428 µm² per transmitter,
or 506487.5424 µm² for eight transmitters. This excludes receivers and clocks.
The 50 pF translator replica contributes 33269.824 µm² per transmitter.
Even replacing that storage leaves a transmitter subtotal of
240328.9504 µm². A replica-only substitution cannot meet the recommendation.

The current literal local-storage implementation adds MIM for the generic
current-cell gate and Miller charge declarations while the process MOS models
also provide terminal charge. `transmit_charge_realization.py` provides an
explicit, unqualified candidate: it assigns 64 charge declarations to the
bound physical MOS gate/source and gate/drain terminals, and retains all 20
other storage declarations. It validates the exported topology and actual
process geometry before creating the two-capacitor local banks. It does not
change the default literal realization.

The `analog.1g.ihp_transmit.charge_ownership` CTest checks complete ownership,
preserved compensation, and rejection of changed transistor connections or
dimensions. `analog.1g.ihp_transmit.charge_storage` checks the candidate's
local geometry, deliberately missing capacitors/substrate taps, and conserved
extraction. Loaded electrical checks must still establish the whole-IC
response before adoption. The area audit does not authorize deleting storage
from the literal implementation or accepting a different transfer response.

The candidate GDS inventory combines the two retained-storage layouts with
the HV-storage replica. Keeping the other transmitter components and copy
counts unchanged gives 58920.496 µm² of MIM for eight transmitters, versus
506487.5424 µm² for the literal implementation. This is still a component
subtotal: receivers, clocks and the final routed chip must be included.
The candidate's loaded electrical qualification remains pending.

Adding the actual shared eight-lane receive-reference layout, reference PLL,
processing oscillator and clock admission gate raises the subtotal to
174418.8125 µm², leaving only 381.1875 µm². The
[physical sampler](receive-sampler-layout.md) adds 1024.3521 µm² per instance.
Eight lanes with thirteen samplers each raise this still-incomplete subtotal
to 280951.4309 µm². Remaining AFE, DAC, comparator, clock distribution and
decoupling storage are excluded. Thus the transmitter candidates alone do
not resolve the whole-chip recommendation. These are measured component
inventories, not an assembled or electrically qualified chip.

`tests/transmit_charge_models.py` composes both complete transmitter circuits
with this current-cell charge ownership, preserving every external port and
accounting for every exported contribution exactly once. It retains the MIM
replica until the separate MOS-replica loaded checks pass. The
`charge_connected_startup` and `charge_connected_mos_tt` CTests reuse the
existing numerical-startup and complete-transmitter checks. Generating these
circuits does not establish a passing transmitter or adopt the candidate.
