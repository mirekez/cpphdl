# Physical transmitter assembly boundaries

[`transmit_physical_export.cpp`](tests/transmit_physical_export.cpp) elaborates
the actual `cascoded_core_emphasized_pam5_driver` and exports its complete
Verilog-A model plus 33 component blocks. It records every original contribution
and the parent electrical node behind each block port.

| Blocks | Instances | Contributions per instance |
| --- | ---: | ---: |
| Translator | 22 | 12 MOS, 24 capacitors |
| Shared translation replica | 1 | 1 MOS, 3 resistors, 1 capacitor |
| Trim and cascode bias | 1 | 7 MOS, 9 resistors |
| Main/history current cell | 8 | 4 MOS |
| Remaining passives | 1 | 2 pullups, 84 capacitors |

The complete C++ IC contains 931 contributions: 304 MOS laws, 14 resistors
and 613 capacitors. The export assigns each contribution exactly once.
[`transmit_physical_partition.py`](transmit_physical_partition.py) independently
normalizes every block's node names and compares its equations with the complete
model. It also checks command order, shared supplies and bias, translated trim,
current-cell steering, and distinct internal storage nodes.

This is a lossless connectivity partition. Selection of a qualified process
implementation, such as the translator's PSP intrinsic-charge implementation,
remains a separate checked operation. The partition itself discards no charge.

## Retaining storage across current-cell boundaries

The existing four-transistor current-cell layouts expose seven external pins
and retain measured internal `TAIL` and `LOWER` conductor anchors. The complete
C++ transmitter also attaches capacitors to those internal nodes. Composing
only the seven-pin cells would lose those connections.

[`transmit_current_binding.py`](transmit_current_binding.py) checks all eight
actual exported cells against the reviewed IHP geometry and the qualified
physical banks. It can expose the existing RC anchors as additional subcircuit
ports without changing any internal resistor, capacitor or transistor equation.
A bare name appearing only in a capacitance record is insufficient: each
anchor must connect to a finite positive wire resistor.

The retained capacitors still require physical placement and routing to those
anchors. `remaining_passives.va` is an export group, not a recommendation to
place all storage in one distant macro. Local steering and cascode storage
belongs near its current cell; shared bias storage belongs near its reference.

## Checks and regeneration

Eight partition tests reject missing blocks, duplicated contributions, swapped
commands or internal anchors, removed or changed storage, and extra unowned
equations. Six binding tests check the actual physical banks and reject changed
connections, dimensions, missing anchor wires and malformed extracted interfaces.
Both maintained CTests pass after the final source changes (1.91 seconds).
They establish connectivity and geometry bindings; the connected loaded TX
transient and full routed transmitter remain to be qualified.

From the repository root:

```sh
cmake -S analog -B analog/build/regeneration-audit
cmake --build analog/build/regeneration-audit \
  --target analog_1g_ihp_transmit_physical_export
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog\.1g\.ihp_transmit\.(physical_partition|current_binding)$' \
  --output-on-failure
```

CTest regenerates the current-cell fixture when required. The binding test
verifies its source and artifact hashes, including extraction and negative-test
evidence. Exports and physical results are generated in test work directories;
they are not required as committed inputs.

## Physical storage and bias bindings

`transmit_storage_layout.py` keeps each cell's ten declared storage
contributions local, including its BIAS-to-VSS branch. It builds separate main,
history and output MIM macros. An explicit `ptap1` contact returns substrate
coupling to VSS. The maintained `analog.1g.ihp_transmit.storage` test passes all
three cases, including strict LVS rejection of removed storage and substrate
contacts, local DRC, and conserved RC extraction (193.37 seconds).
Main/history/output footprints are 26,463.606 / 16,577.3647 / 2,705.5891 µm².

The optional `transmit_charge_realization.py` candidate instead assigns the
eight gate/source and gate/drain approximations per current cell to that cell's
physical PSP devices. Its ownership plan preserves all 84 storage intentions:
64 use intrinsic terminal-charge models and 20 remain explicit storage.
The default literal assembly above is unchanged. The candidate's two-capacitor
main/history banks occupy 1,254.9368 / 1,136.7912 µm² and pass local geometry,
missing-capacitor/tap rejection and conserved extraction. Six switching cases
with the extracted current cells also pass at the original limits. These tests
use ideal gate/bias sources; complete loaded-TX checks are still required.
See [MIM budgeting](mim-area-budget.md) for the whole-chip area issue and CTests.

The literal shared 400 pF MIM implementation fails `MIM.gR`, the PDK's
recommended whole-chip MIM-area maximum. Splitting plates does not remove
that total-area constraint. The assembled process model therefore uses the
existing [MOS bias-storage realizations](mos-storage.md) for the 400 pF and
50 pF intentions. Their geometry and extracted-charge evidence has unchanged
source hashes. Their voltage-dependent equations remain explicit; this does
not identify them with ideal capacitors or establish full-chip density closure.

`transmit_bias_binding.py` also checks the actual C++ trim/cascode reference
against the qualified physical bias macro. Seven maintained guards pass,
including bit swaps, body connections, resistor weights and MOS dimensions.
The connected transmitter still requires loaded electrical and routed checks.

## Connected electrical experiment

`transmit_physical_assembly.py` instantiates all 22 translators, the replica,
trim/cascode reference, eight current cells, their local storage, shared MOS
storage and two line pullups. Its 45 physical instances own all 931 original
contributions exactly once. Each extracted component keeps its wire resistors,
coupling capacitances and substrate connections. Inter-component connections
are ideal at this stage; routing those wires remains a separate requirement.

`tests/transmit_physical_models.py` admits components only after checking their
passing geometry/extraction receipts, source hashes, artifact hashes and C++
bindings. It produces an independently constructed schematic using the same
reviewed process devices and a second model using the extracted component RC.
The dedicated MOSCAP retains its documented LVS adapter and TT model limits.

`analog.1g.ihp_transmit.connected_mos_tt` uses the independently qualified
controller sequence, 100 Ω differential load, 50 Ω command sources, and the
established finite supply impedances and decoupling. Both flows must separately
pass the existing 20 mV weighted-level, 15 mV bias-ripple, supply/current, gate
rail, trim and all-state aperture checks. Startup observations also check both
MOS storage voltage ranges and all 22 translators' 1.65 V LV gate-exposure
bound. Only then are the six plateau observables compared with the existing
10 mV allowance. A simulator exit code alone cannot produce a pass: the runner
checks completion, errors, sample count, time coverage and unchanged inputs.

The connected regression is registered with CTest and its component fixtures;
the ten composition/experiment guards pass. Loaded simulation qualification is
still pending. The short startup diagnostics are not electrical qualification.

```sh
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog\.1g\.ihp_transmit\.connected_' --output-on-failure -j 1
```

CTest regenerates the required components and controller fixture. The loaded
runner can also emit both reviewable experiments using `--emit-only`; that
mode records `netlists_emitted`, never an electrical pass. All exported models,
simulation decks and compressed waveforms remain generated test artifacts.
