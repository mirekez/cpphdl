# Shared physical die

This flow places the as-is repeater in one common die. Setup/hold failures do
not gate placement or routing. It retains the mapped digital cells individually,
eight SRAMs, every elaborated analog law, and the actual IHP padframe/ESD cells.
There are no DSP, PHY or PCS placement fences. The former 19-block presentation
is not an input to this flow.

Analog devices use real IHP PCells, contacted terminals and routing obstructions.
Their fixed leaf locations preserve physical device geometry; digital cells are
placed in the common rows between them. Large transistors use parallel leaves.
Large resistors use explicit series segments; no declared circuit element is
silently dropped. All 104 receiver slices and eight transmit/receive lanes are
included. `shared_analog.py` selects existing process geometries by component
ancestry as well as model parameters. New FLP size choices remain unqualified.

Run from `analog/`, after preparing the IHP PDK and mapped digital checkpoint:

```sh
python3 pdk/1G_repeater_IHPSG13G2/shared_prepare.py --work build/repeater-shared-space
python3 pdk/1G_repeater_IHPSG13G2/shared_run.py --stage all --work build/repeater-shared-space
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/shared_stream.py \
    --work build/repeater-shared-space --stage routed --output build/repeater-shared-space/images
```

The default digital input is `build/repeater-unified-control-repair/placed.odb`;
use `--digital-db` to select another checkpoint with the same cell inventory.
Its reproducible synthesis/placement recipes are `unified_synthesis.py` and
`unified_place.py`. Intermediate `floorplan`, `place`, and `route` stages can
also run separately. Each saves its database before the following stage, and
the runner records failures. `shared_stream.py --stage placed` publishes the
actual placement without waiting for routing or timing closure.

The physical inventory has 67,572 source laws, expanded into 118,019 analog
leaves using 132 reusable masters. The initial die is 11.44 mm square. Ten
explicit 1 GOhm resistors account for 50,000 high-ohmic resistor segments; this
is a costly literal implementation, retained for this as-is placement pass.
Reducing that area requires an explicit circuit/model change and validation.

`analog.pdk.1g_repeater.shared_physical` checks the real sv2v array-bit ordering
and context-dependent device sizing. The prepare step also checks every source
law survives expansion and each physical master pin reaches conductive metal.
`analog.semantics` checks parent/class provenance survives repeated elaboration.

A shared placement or routed candidate is not fabrication signoff. Supply-grid
completion, detailed DRC/LVS, analog extraction/qualification and unresolved
on-chip control/clock producers remain separate requirements. Timing results
are not relabeled as passes. Generated leaves, databases and logs belong under
`build/`; the final GDS and selected images can be published after generation.

## Current checkpoint and routing limit

The saved `connected.odb` has legal placement for 879,909 standard cells and
118,999 fixed physical instances. Independent row containment, orientation,
spacing and overlap checks pass. The database audit checks 337,084 analog
terminals and 1,213 RTL crossings. The published GDS and images use this
checkpoint. They contain one die and the full instantiated circuit inventory.

OpenROAD's full-grid legalizer exceeded available memory. The resumable flow
uses `shared_sparse_legalize.py`, which allocates free row intervals and checks
the finished geometry independently. It preserves the global placement as its
input; its displacement is reported rather than hidden.

Routing remains unfinished. The direct run timed out; subsequent runs with
1,276 non-inverting fanout buffers and conservative routing grids exceeded the
resident-memory guards in both FastRoute and CUGR. The last FastRoute retry
stopped at 4,806 MiB against a 4,800 MiB limit. No routed checkpoint was produced.
The host has approximately 5.7 GiB RAM. This establishes a resource limit for
these runs, not the minimum memory required for successful routing.

`buffered.odb` retains the extra buffers. Every changed sink is checked through
its actual buffer-tree connections to the original source. Clock skew is not
qualified. Global routing temporarily uses a subset of legal M2/M4 tracks to
reduce its planning grid, then restores the original physical tracks before
saving a successful checkpoint. No spacing rule or routing capacity is relaxed.
The grid selection follows the pinned OpenROAD implementation:
[`dbBlock::getGCellTileSize`](https://github.com/The-OpenROAD-Project/OpenROAD/blob/08f67ee5ec/src/odb/src/db/dbBlock.cpp).

To resume on a host with sufficient memory, choose a suitable memory budget:

```sh
python3 pdk/1G_repeater_IHPSG13G2/shared_run.py --stage global-route \
    --work build/repeater-shared-space --memory-mib 12000
python3 pdk/1G_repeater_IHPSG13G2/shared_run.py --stage detail-route \
    --work build/repeater-shared-space --memory-mib 12000
```

The example budget is a limit, not a prediction or guarantee. `--engine cugr`
selects the alternative global router. `--routing-seconds` bounds each routing
stage; `shared_memory_run.py` stops owned jobs before prolonged memory pressure.
Timing closure is not a gate in any of these stages.

To publish a checked placement without waiting for routing:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/shared_stream.py \
    --work build/repeater-shared-space --stage connected --output build/repeater-shared-space/images
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/shared_publish.py \
    --work build/repeater-shared-space
```
