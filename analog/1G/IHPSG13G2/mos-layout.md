# Connected transistor layout

[`mos_layout.py`](mos_layout.py) begins physical analog implementation with a
four-terminal MOS macro. It instantiates the pinned IHP `nmos`, `pmos`,
`nmosHV` or `pmosHV` PCell and connects every gate, source and drain finger. The
PCell's pin markers alone expose only the first gate and first diffusion
stripes; repeating their names would not connect the remaining fingers.

The generator connects alternating diffusion stripes to separate Metal2 buses.
Every gate receives a poly extension, contact, Metal1 landing and Via1 to the
gate bus. A contacted substrate guard ring has its own Metal2 body terminal.
The independent schematic retains the ring's `ptap1` or `ntap1` contact, so LVS uses the
PDK's default tap extraction. Neither labels nor an ideal-tap mode replace
physical connectivity.

Total width and length retain their PSP meaning. Unsupported geometries,
off-grid dimensions and changed PCell inventories fail explicitly. The initial
supported range is 2–10 µm per finger, length 0.45–10 µm for HV and 0.13–10 µm
for LV, and the pinned 10 nm PCell parameter grid. Low-voltage gates get a
wider contacted landing outside active; their channel dimensions remain intact.
Duplicate overlapping shapes in the pinned LV PMOS are treated as one conductor.

```python
import klayout.db as db
from mos_layout import append_hv_nmos

layout = db.Layout()
layout.dbu = 0.001
cell, contract = append_hv_nmos(layout, pdk, "ReceiveFollower", 400, 0.45, 40)
layout.write("receive_follower.gds")
```

`contract` contains the four terminal locations, dimensions, finger and strap
counts, bounding box, and intended MOS/substrate-contact schematic. A parent
must connect those terminals with geometry and verify the assembled netlist.
The helper does not route an entire amplifier or receiver.

The regression runs strict-port transistor LVS and the complete PDK DRC tables,
including antenna and pins, on one- and four-finger HV NMOS and four-finger
HV PMOS, LV NMOS and LV PMOS macros. It also checks the selected sampler
NMOS/PMOS (25 µm / five fingers and 50 µm / five fingers) and dummy devices
(12.5 µm / two fingers and 25 µm / five fingers). The 25 µm / three-finger
alternative is rejected because its finger width is off the PCell grid. All
nine undamaged cases pass local geometry and
strict MOS/tap LVS; the open-finger case is rejected. It separately
records global density violations in these unfilled macros; any other rule
violation fails. Its report explicitly retains `full_chip_drc = false` and
each macro's actual `full_drc_pass` value. Full-chip acceptance still requires
zero violations after integration and fill. An intentionally disconnected gate
finger must fail LVS despite unchanged labels and intended schematic.

```sh
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_mos[.]layout$'
```

No extracted capacitance, resistance, device matching or analog performance
qualification follows from connectivity alone. Those require the connected
physical circuit and its own simulations before area optimization.
