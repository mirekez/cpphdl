# Three-tap TX emphasis

[tx_emphasis.hpp](tx_emphasis.hpp) accepts electrical PRE, MAIN and POST levels,
plus VCM and the P/N outputs. The default coefficients are `[-0.1, 0.8, -0.1]`.
Each tap contributes controlled current to both output wires with opposite polarity.
Each wire also has a 50 Ohm return to VCM and 2 fF output storage. The coefficients
are finite, the cursor must be positive, and their absolute sum cannot exceed one.

With tap symbols `s = (Vtap - VCM) / 0.6 V`, the unloaded differential target is:

```text
V(P, N) = 0.8 V * (-0.1 * s_pre + 0.8 * s_main - 0.1 * s_post)
```

An external 100 Ohm differential load halves that settled voltage. Further channel
and receiver loading changes it again through the coupled equations. The gm cells
are ideal VCCS models with explicit finite output impedance/storage; they do not
establish a fabricated driver's power, noise or compliance.

The [transmitter](phy.hpp) supplies real clocked tap history from the serializer.
The unit bench instead drives known aligned tap symbols, then checks the loaded
sum analytically in each solver. The transmitter bench separately checks that its
stored history produces the expected FIR output. Tests: `emphasis`, `transmitter`,
`link`, `link_loaded`, each through both flows and comparison. Module:
[TenGTxEmphasis.va](generated/TenGTxEmphasis.va).
