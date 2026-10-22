# Generated 1G models

The `.va` files are exports of the electrical ICs listed in [the module guide](../README.md).
Process-specific electrical exports are in [`../IHPSG13G2/generated/`](../IHPSG13G2/generated/).
The `.sv` files describe the separate digital hardware blocks:

| Model | C++ source | Clocks |
| --- | --- | --- |
| `ReceiveClockAligner.sv` | `receive_alignment.hpp` | `ref_clk`, `rx_clk` |
| `PCSSymbolMapper.sv` | `pcs.hpp` | `clk` |
| `PCSSideStream.sv` | `pcs.hpp` | `clk` |

Compile `Predef_pkg.sv` before the digital modules. These are partial PHY building
blocks; they are not a complete Clause 40 PCS or a physical analog receiver.
See [PCS](../pcs.md), [clock alignment](../receive_alignment.md), and
[verification results](../tests/hardware-validation.md) for interface and scope.

Regenerate from the repository root:

```bash
cmake --build analog/build --target generate-1g-veriloga
build/cpphdl --primary_clock ref_clk 125000000 --secondary_clock rx_clk 125000000 \
    --generated-dir analog/1G/generated analog/1G/receive_alignment.cpp -- -Iinclude
build/cpphdl --generated-dir analog/1G/generated analog/1G/pcs.cpp -- -Iinclude
```

The electrical catalog regression regenerates and compares all `.va` files.
Digital regressions independently regenerate RTL, execute the actual C++ tests
against it, and compare native, RTL and available IHP mapped-gate trajectories.
