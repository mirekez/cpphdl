# Generated digital forwarding core

`RepeaterCore.sv` is the root converter's export of [repeater.hpp](../repeater.hpp).
Compile `Predef_pkg.sv` first. The core uses one 125 MHz `ref_clock` and expects
already aligned GMII words; it does not contain complete copper PHYs or pads.
[The implementation guide](../README.md) documents the ports and physical flow.

Regenerate from the repository root:

```bash
build/cpphdl --primary_clock ref_clock 125000000 \
    --generated-dir analog/pdk/1G_repeater_IHPSG13G2/generated \
    analog/pdk/1G_repeater_IHPSG13G2/repeater.cpp -- -Iinclude
```

The physical regression regenerates RTL from the C++ source and checks native,
RTL, mapped and routed gates before DRC/LVS and timing qualification of the core.
Its artifacts stay in ignored `results/`; these checked-in exports are readable
source-derived models, not a complete chip or a fabrication-qualified padframe.
