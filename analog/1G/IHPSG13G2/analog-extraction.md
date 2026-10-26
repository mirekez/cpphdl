# Analog layout extraction

`extract_analog.py` reads the physical GDS with the pinned IHP Magic deck.
KLayout remains the DRC/LVS authority. The extractor preserves individual MOS
fingers and actual MIM plates; it produces a capacitance-only netlist, raw
distributed RC, and a separate capacitance-conserving RC netlist. All three
remain available with their source, tool, PDK and artifact hashes.

Build the extraction tool from the root CMake build:

```sh
cmake --build build --target prepare-ihp-extraction
cmake -S analog -B analog/build
ctest --test-dir analog/build -R '^analog\.1g\.ihp_extraction\.' --output-on-failure
```

The first command assumes a root build configured with the analog targets.
For a standalone analog build, use `cmake --build analog/build --target
prepare-ihp-extraction` instead. `prepare_magic.py` pins Magic 8.3.685 to an
upstream commit and verifies the archive SHA-256. It builds in `analog/out/`.
If Tk headers are absent on Ubuntu 22.04 amd64, it verifies and unpacks the
Ubuntu packages locally; it does not install system packages. Other hosts
need their own Tcl/Tk development packages.

Example extraction, run from `analog/` with a fresh output directory:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/extract_analog.py \
  --gds /path/to/gate.gds --top ProcessingClockGate \
  --ports VDD VSS CLOCK ENABLE OUT --substrate VSS \
  --pdk pdk/IHPSG13G2 \
  --magic out/magic-toolchain/install/bin/magic \
  --work build/gate-extraction
```

`capacitance.spice` retains inter-net coupling with ideal wires.
`distributed.spice` is the untouched Magic output. `conserved.spice` keeps its
resistor network and device dimensions while correcting a measured extractor
defect. Use the latter only after inspecting the accompanying conservation
audit, electrical test results and limitations below.

## Capacitance conservation

The gate reproduced [Magic issue #550](https://github.com/RTimothyEdwards/magic/issues/550):
the distributed writer retains inter-net coupling and additionally folds each
coupling into both substrate loads. This is not a physical extra capacitor.
The raw gate total was 1.934 pF versus 0.915 pF in capacitance-only extraction.

`extraction_capacitance.py` first collapses only the extracted wire resistors
for comparison with the reference capacitance matrix. It rejects shorts,
unmapped endpoints and missing/changed coupling. For each net, the substrate
load must either already match, or differ by precisely the sum of its coupling
loads within 0.01% extraction rounding. Only the second case is corrected by
scaling that net's distributed substrate capacitance. The corrected matrix
must match the original within 0.005%; no resistor, MIM, MOS or coupling value
is changed. Raw output is always retained. Eight guard tests include independent
common-mode charge accounting and deliberately damaged networks.

The real-geometry MIM regression checks one 4 fF plate and a two-plate 2 fF
series element. Both retain their process dimensions and nonzero stray
capacitance. The series junction remains a separate electrical node with its
own substrate load. A second copy of the MIM device capacitance would exceed
the allowed stray budget and fail this regression.

## Qualification boundary

The preparation script also corrects the pinned Magic SI formatter. Its
original branch for magnitudes below `1e-18` left those numbers unscaled and
then printed fixed decimal places, silently turning nonzero sub-attofarad
loads into zero. On the oscillator's VDD network that lost about 0.341 fF
and failed the capacitance-conservation check. The local correction prints
those values in scientific notation. It changes serialization only; source
hashes before and after the correction and the recipe hash are recorded in
`prepare.json`. No extraction coefficient, geometry or PDK file is changed,
and the conservation tolerances remain unchanged.

The extractor accepts wrapped SPICE port lists and preserves explicit process
resistors and antenna diodes. A capacitor between two points of one logical
ground net remains in the distributed network: it has zero energy only when
those points are collapsed to one ideal conductor for the matrix comparison.
It is not deleted from simulation. The oscillator extraction and the separate
single/series MIM extraction regression pass with the corrected formatter.

For simulation, `reduce_extracted_wires.py` can eliminate unloaded internal
resistor nodes by a Schur complement. It protects every port, device terminal,
capacitor endpoint and requested probe. It neither merges storage nodes nor
drops small capacitance. Its independent nodal-solve regression checks terminal
currents over randomized resistor values and imposed boundary voltages. The
original extracted files remain the provenance source.

An oscillator-specific electrical audit additionally found 65 coupling
endpoints attached to resistor-body names absent from the SPICE device/wire
connections, matching [Magic issue #559](https://github.com/RTimothyEdwards/magic/issues/559).
`extracted_connections.py` rejects this case before oscillator simulation.
The capacitance-only and distributed matrices can agree while both contain
this disconnect; conservation is necessary but does not establish electrical
connectivity. Floating fill islands require a separate explicit model and
are outside this intentional-device macro check. `resistor_body_extraction.py` supplies a separately audited candidate binding:
two R3 segments attach each body coupling to an electrical midpoint. Geometry,
contact resistance and total intrinsic charge are conserved; duplicate Magic
body-substrate shunts are removed and all external coupling is retained.
The raw extraction is never overwritten. Nine process-model DC and low-frequency
common-mode charge cases and the connection/charge-accounting guards pass.
These checks establish the declared representation, not field accuracy.
Contact-head partition and independent capacitance calibration still need
resolution before extracted oscillator sign-off.

This is distributed resistance with area-distributed substrate capacitance
and **lumped inter-net coupling**, following Magic's net attachment points.
The conservation check establishes consistency with the capacitance-only
extraction, not agreement with a field solver. It cannot repair an incorrect
technology coefficient or coupling geometry. In particular,
[upstream issue #557](https://github.com/RTimothyEdwards/magic/issues/557)
reports a sidewall-coefficient discrepancy; independent interconnect calibration
remains required before sign-off. The original pinned PDK is unchanged.

The pinned Magic source applies a sidewall factor of one half in both parser
keywords and again in the existing micron/lambda conversion. An isolated
candidate removes only the parser factors, preserving the older per-edge
conversion. `magic_sidewall.py` checks the exact upstream source hash before
changing those two expressions; `prepare_magic.py --full-sidewall` builds it
into a separate installation. Existing tools and PDK coefficients are retained.

The independent `tests/magic_sidewall.py` oracle copies the PDK into its own
work directory and sets only the sidewall offsets to zero. Its expected
coupling is then exactly the deck coefficient times facing length divided by
gap. It checks seven metals, both parser keywords, rotated/reflected wires,
length/gap scaling and shielding by an intermediate conductor. Runtime library,
fixture technology, source and GDS hashes are retained. The original extractor
fails all 70 nonzero checks at approximately half the expected value; its
14 shielding checks pass. The corrected candidate passes all 84 checks in
CTest. Single and series MIM device/stray extraction also pass with that binary.

```sh
cmake --build build --target prepare-ihp-extraction-sidewall
cmake -S . -B build
ctest --test-dir build -R '^analog\.1g\.ihp_extraction\.sidewall$' --output-on-failure
```

The candidate lives in `out/magic-sidewall/`; the default extractor is not
silently replaced. This establishes coefficient handling, not accuracy of
the empirical sidewall fit, nonzero-offset rounding, dielectric stack or
field calibration. The next hierarchical PLL electrical run uses it;
older extracted-clock trajectories remain evidence for their recorded tools.

An independent field solver is now prepared by `prepare_fastercap.py`:
FasterCap 6.0.9 from the release recommended by the
[KLayout-PEX documentation](https://iic-jku.github.io/klayout-pex-website/doc/doc.html).
The Linux x86-64 binary archive and its locally unpacked Ubuntu wxBase runtime
are SHA-256 pinned. Nothing is installed into the system or PDK.

```sh
cmake --build build --target prepare-ihp-field-solver
cmake -S . -B build \
  -DCPPHDL_IHP_FIELD_SOLVER_PREPARATION="$PWD/out/fastercap-toolchain/prepare.json"
ctest --test-dir build -R '^analog\.1g\.ihp_extraction\.field_solver$' --output-on-failure
```

These commands assume `analog/` as the current directory. The numerical test
passes four independent analytical cases using `C = 4 pi epsilon R` for an
isolated sphere. Refining its surface from 512 to 2,048 triangular panels
reduces the capacitance error from 0.766% to 0.191%. Permittivity scaling by
3.9 and radius scaling by two with a translated origin also pass. Each solve
must converge independently. The receipt is
`build/1G/IHPSG13G2/tests/results/field_solver/checks.json`.
This establishes the solver's baseline units and electrostatics, not the IHP
layout stack. The pinned PDK's openEMS stack supplies metal heights and bulk
dielectrics but does not define the oscillator's complete FEOL/poly geometry.
Stack-specific coupling, resistor contact-head partition and extracted routed
geometry still require their own calibration.

`prepare_kpex.py` prepares KLayout-PEX 0.6.12 in an isolated environment to
translate GDS into the field solver's geometry. `kpex-requirements.txt` pins
every downloaded wheel and SHA-256 for Linux x86-64 CPython 3.14. The preparation
checks installed wheel contents and records the bundled IHP stack separately
from the repository's pinned PDK. This stack is an independent comparison
model; it is not a foundry-qualified extraction deck.

Run the reproducible two-wire comparison from `analog/`:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/prepare_kpex.py \
  --destination out/kpex-toolchain
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/metal_coupling.py \
  --solver-preparation out/fastercap-toolchain/prepare.json \
  --geometry-preparation out/kpex-toolchain/prepare.json \
  --magic out/magic-precise/install/bin/magic \
  --klayout out/repeater-toolchain/bin/klayout --pdk pdk/IHPSG13G2 \
  --work build/metal-coupling
```

Both tools receive the same two 20 µm long, 0.5 µm wide Metal1 wires.
The comparison varies their gap and retains GDS, field geometry, Maxwell
matrices, SPICE and hashes. FasterCap coordinates are in micrometres, so raw
matrix coefficients are multiplied by `1e-6` to obtain farads. The matrix
reader checks convergence, final matrix signs, reciprocity and charge to
infinity. Only explicitly recognized coarse-mesh matrix warnings can be
superseded by a subsequent successful refinement; geometry errors remain
fatal. Its separate CTest rejects twelve damaged or unsupported outputs.

The first spacing sweep completed as a **characterization**, with the field
solver's relative matrix-change criterion set to 1%:

| Gap (µm) | Magic coupling (fF) | Field coupling (fF) |
| --- | ---: | ---: |
| 0.2 | 1.915670 | 2.646190 |
| 0.4 | 0.821000 | 1.563915 |
| 0.8 | 0.383130 | 0.905744 |

The receipt is `build/clock-integration/metal-coupling-sweep-v2/checks.json`.
The field stopping criterion is not an absolute error bound: tightening it to
0.2% changed the 0.2 µm result to 2.506825 fF. A 0.05% refinement reached the
1 GiB process memory cap and failed, with its original log retained. The finite
substrate/dielectric boundary also needs a domain-size convergence check.
These results demonstrate a spacing-dependent disagreement; they do not
justify a global multiplier or qualify the clock macro capacitances.

The corrected extractor has now been compared with the corrected FasterCap
source build at a 0.2% relative matrix-change stopping criterion. For the same
Metal1 wires, the corrected Magic/field coupling pairs are 3.831330/2.506825 fF
at 0.2 µm gap, 1.642000/1.469885 fF at 0.4 µm, and 0.766270/0.830727 fF at
0.8 µm. Removing the parser defect does not make the empirical fit exact.

`metal_coupling.py` now accepts `--metal`, `--width`, `--length` and positive
`--gaps` inside the extraction halo, so calibration can use the actual routing
dimensions. The row router uses 0.4 µm Metal4 columns with 0.4 µm spacing and
0.24 µm Metal5 tracks with 0.76 µm spacing. Fresh 20 µm wire fixtures give:

| Geometry | Mutual coupling: Magic / field (fF) | Same-direction load error | Quiet-neighbor load error | Opposite-direction load error |
| --- | ---: | ---: | ---: | ---: |
| Metal4, width/gap 0.4/0.4 µm | 2.476300 / 1.784515 | −23.1% | +17.8% | +26.1% |
| Metal5, width/gap 0.24/0.76 µm | 1.362280 / 1.093880 | −25.4% | +2.7% | +10.6% |

These load errors compare signal A with the field model, not measured silicon.
`field_matrix.grounded_pair` retains both signal diagonals and symmetrizes the
mutual term. Substrate and infinity are held at AC ground, so the Maxwell row
sum remains part of the ground capacitance. Equal, stationary and opposing
neighbor transitions respectively load the signal by `Cg`, `Cg + Cm` and
`Cg + 2*Cm`. The independent capacitor-network regression checks all three
cases and rejects an unknown or ambiguous grounding boundary.

The receipts and retained original recipe sources are under
`build/clock-integration/sidewall-coefficient/`; `switching-load-comparison.json`
checks their waveform-independent matrix/netlist artifacts before comparing
them. The results demonstrate separate ground and coupling model errors.
No single capacitance multiplier, relaxed clock criterion or calibrated-chip
claim follows from them. Mesh/domain convergence and complete routed-geometry
validation remain required.

The disk-backed retry is rejected. An independent 1 µm sphere, represented by
the same 8,192 triangles in both runs, gives 0.111270 fF with in-memory
interactions versus the analytical 0.111265 fF. Disk-backed interactions give
7.60604 fF. Both runs use the same fixed mesh settings, Galerkin scheme and
binary; only the memory threshold differs (`-f2` versus `-f100`). The comparison
is recorded in `build/clock-integration/field-solver-memory-mode-comparison.json`.
`field_matrix.py` rejects results that entered this unqualified mode, even
if the process exits successfully or prints a converged matrix. A caller can
enable it only after verifying a separate solver qualification receipt.

Source review found that `ComputePanelPotentials_2fast()` calculated its
block offset before the loop over disk blocks. After loading a later block,
the multiplication still used the initial offset. `prepare_fastercap_source.py`
moves that calculation into the block loop, after loading the block. It pins
all three source archives, the local wxBase and BLAS packages, and hashes of
the source before and after this correction. It rejects any other changes to
the pinned build inputs. The original binary installation remains unchanged.

An unmodified source build reproduces the sphere failure. The corrected build
gives 0.111270 fF in both memory modes on the same mesh. The maintained CTest
also compares one- and two-conductor matrices in both modes and requires that
the disk cases actually use disk blocks:

```sh
cmake --build build --target prepare-ihp-field-solver-source
cmake -S . -B build \
  -DCPPHDL_IHP_FIELD_SOURCE_PREPARATION="$PWD/out/fastercap-source-build/prepare.json"
ctest --test-dir build -R '^analog\.1g\.ihp_extraction\.field_solver_disk$' --output-on-failure
```

That maintained regression passes: both memory modes produce the same printed
matrix for one and two conductors, including mutual capacitance. `metal_coupling.py --disk-proof` accepts
only a passing regression for the exact supplied preparation and verifies its
source/tool hashes. `--ooc-threshold 100` can then keep the interaction matrix
on disk while the process remains bounded to 1 GiB. This fixes a numerical
execution defect; it does not change the IHP stack or extract new ASIC devices.

These checks do not qualify a full-chip GDS, density fill, extracted clock
tree, aperture/metastability, IR/EM, package parasitics or Ethernet compliance.
SPICE benches must explicitly save only their required probes: retaining all
nodes of the distributed network can exhaust memory without producing a
complete waveform.

## Clock-gate electrical regression

`tests/processing_clock_gate_extracted.py` uses the native clock-gate bench's
waveform sources and finite source impedances. It connects the actual routed
controller's AND2, NAND2 and INV8 masters to the extracted admission pin,
then tests the gate at a 200 fF output load. It saves only four terminal probes.
The same 96 raw-clock transitions and 32 admitted pulses must satisfy the
original phase, rail, 3–4.1 ns pulse-width and 400 ps delay limits. Each of
TT/27 °C/1.2 V, SS/125 °C/1.08 V and FF/−40 °C/1.32 V is checked separately;
MIM models use typical, worst and best capacitance respectively.

Set `CPPHDL_1G_PROCESSING_GATE_EXTRACTION` to the extraction directory and
`CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION` to the qualified controller's
physical directory when configuring CMake. Then run:

```sh
ctest --test-dir analog/build \
  -R '^analog\.1g\.ihp_processing_clock_gate\.extracted$' --output-on-failure
```

This electrical test checks extraction provenance and the selected controller
masters. Its clock source is still external, and the upstream controller wires
and full receiver clock tree are not included. Pair it with the gate's DRC/LVS
receipt; a passing waveform does not independently establish mask connectivity.

`tests/processing_clock_gate_extracted_charge.py` measures the external ENABLE
pin current through a finite 50-ohm source at 100 ps command edges. It keeps
the conserved distributed circuit, the physical MIM models and 200 fF output
load. Both held raw-clock states are tested independently at all three declared
corners. Each trace must cover 90 ns, with 14 post-startup transitions and 12
complete integration windows; returned charge is counted in the absolute load.
The six cases pass with a maximum of 71.509 fF against 80 fF. The remaining
8.491 fF belongs to the external controller-to-gate connection, not spare load
already proven after full-chip routing. Run the registered
`analog.1g.ihp_processing_clock_gate.extracted_charge` CTest with the same cache
paths. Completed raw cases can be rechecked only if their regenerated stimulus
decks match byte for byte; the original failed header-parsing receipt is kept.
