# IHP 1G repeater tests

Design and flow: [`pdk/1G_repeater_IHPSG13G2/`](../README.md).

`padframe.py` assembles actual IHP I/O cells and checks the sixteen MDI pads,
reference/control/status pins, four supply nets, corner/pad placement, power
abutment connectivity and expected clamp structures. Removing one pad must fail
the geometry check. This does not assert that the current padframe passes LVS.
`physical_guards.py` verifies that absent inputs and a GMII-only interface cannot
qualify as a physical copper chip. [Chip verification](../chip.md) explains the
actual full-deck DRC/LVS runner and unresolved padframe mismatches.

`core.cpp` runs 20,513 cycles with independent full-duplex streams, 24 frames
per direction (64/1518 bytes plus preamble/SFD), 12-octet gaps, all 1024 packed
data/valid/error values, all enable/link combinations and in-flight reset.
The same source runs natively and against generated RTL. It saves achieved
native vectors for subsequent gate comparisons.

`check_vectors.py` independently verifies forwarding, sequence coverage, frame
lengths, preamble/SFD, 48 CRC32 values and minimum IFG. `gates.v` compares every
vector against mapped and post-route gates. These are functional gate tests;
clock timing is checked separately with Liberty/SPEF STA.

`test_flow.py` rejects absent/malformed/violating DRC reports, bad timing,
missing extraction and incomplete-chip requests.

`finish_timing.py` runs the real IHP libraries and OpenROAD on an overloaded
XNOR and a short register path. It first requires actual slew and hold failures,
then exercises output isolation, placement, RC rebuilds and final network repair.
Acceptance requires legal placement, positive setup and hold, and no electrical
violations. This small regression covers the repair mechanism; the complete
receiver still needs detailed-route extraction and independent physical checks.

```bash
cd ~/cpphdl
cmake -S analog -B analog/build -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON
cmake --build analog/build --target analog_1g_repeater_core_test
ctest --test-dir analog/build -R '^analog\.pdk\.1g_repeater\.' --output-on-failure
```

The default cases are native, frame and integrity tests. After preparing the
PDK and tools, enable the complete physical implementation regression:

```bash
cmake -S analog -B analog/build -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON \
  -DCPPHDL_REPEATER_PHYSICAL_TESTS=ON
ctest --test-dir analog/build -R '^analog\.pdk\.1g_repeater\.' --output-on-failure
```

The `.physical` case executes C++RTL through actual GDSII and independent
report checks. It needs the root `build/cpphdl` converter, Verilator, a compiler
and prepared physical tools. It downloads nothing, runs serially and has a
20-minute timeout.

CTest retains artifacts in its binary directory's `physical/` subdirectory.
A direct flow run uses `pdk/1G_repeater_IHPSG13G2/results/core/`. Both retain
vectors, RTL/netlists, DEF/OpenDB, CDL, SPEF/SDF, GDS and KLayout databases.
These tests verify the digital core, not an unimplemented copper PHY.
