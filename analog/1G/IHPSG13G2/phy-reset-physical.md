# PHY reset-controller implementation

`PHYResetControl` generates reset release separately for the reference, ADC and
processing clock domains. Each domain must observe its own running clock before
releasing reset. Loss of qualification asserts reset even if a clock stops.
The C++ implementation is in [phy_reset_control.hpp](../phy_reset_control.hpp).

The physical flow consumes the current native/RTL/mapped-cell regression receipt,
checks its source and netlist hashes, and routes the same mapped controller. It
then replays the functional vectors against the routed netlist, extracts nominal
wire RC for timing, and checks the filled GDS against the pinned IHP DRC and LVS
decks. A stale or failed functional receipt cannot qualify a new layout.

## Reproduce

Configure the analog CMake build with `CPPHDL_1G_PHYSICAL_TESTS=ON`, the prepared
IHP PDK and physical toolchain, and the current converter and Verilator paths.
Build `analog_1g_phy_reset_control`, then run:

```sh
ctest --test-dir build -R '^analog\.1g\.ihp_reset\.physical$' --output-on-failure
```

CTest runs the `analog.1g.phy_reset_control.cells` prerequisite automatically.
Its receipt includes the mapped work directory, avoiding a fixed temporary-run
path. The physical test creates a fresh `PHYResetControl-*` directory under
`1G/IHPSG13G2/tests/results/phy_reset_physical` in the selected build tree.
`physical_module.json` records input/artifact hashes and every qualification
stage; the GDS and reports stay in that build directory.

## Qualified local result

The 2026-10-26 run passed 153,600 functional observations over 10,240 stop/start
schedules in native C++, RTL and mapped cells. The 140 × 140 µm routed macro
also passed exact functional replay, zero-violation filled DRC (868 categories,
all 37 main tables, density and maximum rules) and strict LVS (1,710 devices).
Nominal extracted setup slack was +2.176 ns and hold slack +0.270 ns.

The timing contract uses an 8 ns reference clock, 4 ns ADC clock and
1000/136 ns processing clock, 0.1 ns uncertainty and 80 fF output loads.
The clocks are treated as asynchronous. Output timing is checked against each
output's own clock; it is not checked against one unrelated reference domain.

This qualifies the local controller. The downstream reset trees, reset
recovery/removal at their sinks, synchronizer MTBF, coupling from final fill and
chip integration still require verification. These results do not establish
that the full PHY or repeater ASIC is complete.
