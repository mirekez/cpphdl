# Recorded core implementation — 2026-10-17

**110/110 combined 1G/PDK CTests passed** in 535.27 s, including all six repeater
cases: native C++RTL, independent frame checks, evidence-integrity checks,
physical-verification failure guards, padframe assembly, and the full core flow.
The physical case passed in 137.62 s and ran 27 stages in 137.17 s. Tests are in
[`tests/`](tests/README.md).

The complete copper PHY and chip integration remain unimplemented. New partial
PCS, receive alignment, driver and padframe work is documented in [chip.md](chip.md),
including the failed padframe physical checks. The full-chip target exits with
code 2 and records blockers in `results/chip/flow.json`.

## Produced artifact

`results/core/repeater_core.gds` contains a routed, filled **120 x 120 um core**,
224,048 bytes. SHA256:

```
31e01acc4c35971e4f79845a58d58ff1392fdb4a0a62f599afbedb909e240d08
```

`results/core/flow.json` records `core_checks_pass` and
`full_1000base_t_chip: false`, the stages, selected input/output hashes and
missing chip work. Source is [`repeater.hpp`](repeater.hpp), converted by the
root `build/cpphdl`; no generated RTL was edited.

## Functional evidence

Native C++, generated RTL, mapped gates and the post-route netlist passed the
same forwarding behavior. Each ran **20,513 cycles** with 39,380 forwarded
octets, 523 propagated error indications and nine reset cases. Separate Python
checks verified 48 frames with independent CRC32, preamble/SFD, lengths and
minimum 12-octet gaps. The two directions use different payloads.

Inputs are reference-clock-aligned GMII words. The core has one 125 MHz clock
and one registered forwarding boundary. These checks do not validate an absent
receive-clock alignment circuit or copper transmission.

## Physical evidence

| Check | Result |
| --- | --- |
| Full IHP KLayout DRC | Zero violations; 868 unique rule categories including antenna, off-grid and angle |
| Density | All nine required material layers within limits |
| Transistor LVS | 13 matched circuits, all 48 top-level signal/supply ports, 1762 instantiated transistor devices |
| Routed RC | OpenRCX extracted 125 nets; SPEF retained |
| Setup slack | +5.91 ns against the 8 ns clock period |
| Hold slack | +0.79 ns |
| Slew/capacitance/fanout | No violations after repair |

Density percentages: Activ 39.41, GatPoly 26.77, Metal1 48.00, Metal2 39.90,
Metal3 42.04, Metal4 47.07, Metal5 56.25, TopMetal1/2 43.75. Density rules
remained enabled. The official DRC runner executed main, extra, pin, forbidden,
antenna, off-grid and angle checks. LVS used actual transistor CDL masters in
deep mode, preserving standard-cell pin-label scope, without a port-mismatch
waiver or net-only comparison.

The flow corrected four fanout violations, missing density fill in an unfilled
core and undersized intermediate stacked-via landings. Local routing LEF and
fill adaptations are described in [README](README.md). Installed PDK files
and DRC rule limits remained unchanged.

## Versions and limits

- IHP PDK: `89d42ee6d2fd9b50b86a1aada6fee6673c3e6008`.
- OpenROAD `26Q2-1164-g08f67ee5ec`; KLayout 0.30.5.
- Yosys 0.9; sv2v 0.0.13; Icarus 11; Verilator 5.050; GCC 11.4.
- Synthesis: IHP nominal 1.2 V/25 C cells.
- Timing: slow 1.08 V/125 C and fast 1.32 V/-40 C cells, propagated clocks,
  nominal routed interconnect RC.
- IHP OpenRCX model from OpenROAD flow scripts revision
  `7bcaf475c884e15f674e428aa04dbf92bd0426ed`, SHA256
  `ffda02d57a6ecb39b02268be9511d89ef072911f17bd2dcd095f3f3cbeb5a905`.

Timing omits post-fill coupling extraction, PHY/pad/package timing and full-chip
power/noise/mismatch analysis. The core has no padframe, ESD or sealring. It is
not a complete or fabrication-qualified Ethernet repeater.

## Reproduce

Prepare the tools and PDK as documented in [README](README.md), then:

```bash
cd ~/cpphdl
cmake -S analog -B analog/build -DCPPHDL_REPEATER_PHYSICAL_TESTS=ON \
    -DCPPHDL_REPEATER_PHYSICAL_WORK="$PWD/analog/pdk/1G_repeater_IHPSG13G2/results/core"
cmake --build analog/build --target analog_1g_repeater_core_test
ctest --test-dir analog/build -R '^analog\.pdk\.1g_repeater\.' --output-on-failure
```

The recorded log is `analog/out/1g-final-ctest.log`. GDS timestamps can change its
hash on another run. Ignored local artifacts:

- `results/core/{native,rtl-test,mapped-test,postroute-test,vectors-check}.log`.
- `results/core/{mapped.v,postroute.v,physical.v,core.def,core.odb,core.cdl}`.
- `results/core/{core.spef,core.sdf,setup.rpt,hold.rpt,electrical.rpt}`.
- `results/core/drc/repeater_core_filled_RepeaterCore_full.lyrdb` and per-rule
  logs, including measured density.
- `results/core/lvs/repeater_core_filled.lvsdb` and extracted transistor netlist.
