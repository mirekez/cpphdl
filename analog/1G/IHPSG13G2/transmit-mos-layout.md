# Transmitter transistor banks

[mos_bank_layout.py](mos_bank_layout.py) constructs the large HV NMOS devices
used by the cascoded PAM-5 driver. It preserves total channel width and length,
uses 5 µm fingers, and splits devices into guarded banks of at most 100 fingers.
The 1,500 µm cascode therefore contains three connected 500 µm banks.

The 10 µm finger candidate exceeded the PDK's maximum active-density limit.
Simply increasing the number of fingers in one PCell also failed: the pinned
cell generator changed dimensions beyond its supported range. The banked layout
avoids both failures using physical geometry; it does not waive maximum-density
rules or change the intended transistor strength.

Each diffusion stripe connects through two Via1 landings and parallel cuts
at Via2, Via3, Via4 and TopVia1 to a 2.1 µm TopMetal1 collector. Drain and source
ports are at the centers of these collectors. Gate and substrate-contact ports
are on Metal2. Internal banks have no external pin markers: a second lower-metal
port could let extraction bypass part of the intended via resistance.
Callers must use the returned `terminals` entries, including their layer numbers.
Connecting all transmitter current through one narrow lower-metal landing would
defeat the collectors.

The prepared PDK's `libs.doc/doc/SG13G2_os_process_spec.pdf`, revision 1.2,
section 2.15, specifies 2 mA/µm for Metal2 wider than 0.3 µm, 15 mA/µm for
TopMetal1, and 0.4 mA per Via1–Via4. The original 0.4 µm lower-metal collection
bus could not carry the nominal 7.5 mA main-cell current within those limits.
Distributed connections avoid that single collection bottleneck. Current
sharing, local crowding, IR drop, transient current, temperature and the final
driver connections still need extracted verification; this is not EM signoff.

## Regression

With the prepared PDK and physical tools, run:

```sh
ctest --test-dir build -R '^analog\.1g\.ihp_transmit\.mos_dc$' --output-on-failure
```

CTest first runs the geometry and extraction prerequisites. The cases cover
total widths 400/200 µm for main/history steering,
450/150 µm for current sinks, and 1,500/500 µm for cascodes, all at 0.45 µm
channel length. An independent schematic contains one transistor with the
intended total width and explicit parallel guard contacts sharing the substrate.
Strict LVS must match the physical fingers and all four ports. A deliberately
removed cascode bank must fail that comparison.

The extraction check requires every physical 5 µm finger, exactly one external
location per port on its intended layer, and connected conserved RC. The DC
check compares that circuit against independently instantiated 5 µm PSP fingers
of equal total width. It sweeps the gate at a 1 V drain bias and grounded source
and body, at TT and 27 °C. Current error must remain within 5% at half, nominal
and four-thirds branch current (nominally 7.5 mA main, 2.5 mA history).
This is a local wiring-loss budget; Ethernet output conformance requires the
connected-driver checks. Transient settling and PVT are separate requirements.

The initial end-fed cascode lost 8.8% current at the 7.5 mA reference point.
Center feeding reduced the measured loss to 4.2% without enlarging its footprint.
The steering cell still lost 5.1% at nominal current with single via chains;
parallel cuts address that remaining series resistance. The complete geometry,
extraction and DC chain must pass again for each physical change.
The parallel-via version passed all three CTests on 2026-10-26. Across all six
sizes and all three current points, the worst measured current error was 3.54%.

Each intact case runs complete local DRC, including antenna, pins, maximum
rules and density. Minimum metal-density violations are recorded because these
are unfilled component cells; other violations fail the regression. Integration
must add and verify actual fill. A passing component receipt does not mean the
whole transmitter or chip is DRC clean.

GDS, independent schematics, reports and source/artifact hashes are regenerated
under the build tree's `1G/IHPSG13G2/tests/results/transmit_mos_collectors`,
`transmit_mos_extraction` and `transmit_mos_dc`. Every run uses a fresh directory;
the parent `checks.json` points to the latest run, including failed runs.
The collector-equipped components remain separate from the complete driver
layout until its routing, passives, substrate connections and extracted
electrical behavior are verified.
