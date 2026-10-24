#!/usr/bin/env python3
"""Check actual pad cells, pinout, power abutment and missing-pad rejection."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import unittest
from unittest.mock import patch
import klayout.db as db

SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE))
spec = importlib.util.spec_from_file_location("repeater_padframe", SOURCE / "padframe.py")
padframe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(padframe)


class Padframe(unittest.TestCase):
    def test_larger_core_uses_physical_fillers_without_inventing_pads(self):
        result = padframe.assemble(args.pdk.resolve(), args.work.resolve() / 'larger_core',
                                  common_ground=True, core_size=663)
        self.assertEqual(result['core_opening_um'], 663)
        self.assertEqual(result['core_bbox_um'], [320, 320, 983, 983])
        self.assertEqual(result['die_um'], 1303)
        self.assertEqual(result['cells']['sg13g2_IOPadAnalog'], 16)
        self.assertEqual(len(result['ports']), 43)
        self.assertEqual(result['abutment_rail_components'], dict.fromkeys(padframe.RAILS, 1))
        self.assertGreater(result['cells']['sg13g2_Filler200'], 0)
        self.assertGreater(result['cells']['sg13g2_Filler400'], 0)
        # Exactly 23 um of extra real spacer cells per side. Their widths
        # come from the PDK LEF, rather than the numeric suffix of the cell.
        widths = {'sg13g2_Filler200': 1, 'sg13g2_Filler400': 2}
        self.assertEqual(sum(widths.get(cell, 0) * count for cell, count in result['cells'].items()), 4 * 23)
        self.assertFalse(result['full_1000base_t_chip'])

        # Enlarging the ring must also move the decap supply connections to
        # its new east edge; pad-to-pad abutment alone cannot detect this gap.
        layout = db.Layout()
        layout.read(str(args.work / 'larger_core/repeater_padframe.gds'))
        decap = layout.cell('RepeaterCoreDecoupling')
        lef = (args.pdk / 'ihp-sg13g2/libs.ref/sg13g2_io/lef/sg13g2_io.lef').read_text()
        for net, target_layer in (('vdd', 30), ('vss', 50)):
            rails = db.Region()
            for item in result['placements']:
                transform = db.Trans(item['rotation'] // 90, False,
                                     round(item['x_um'] / layout.dbu), round(item['y_um'] / layout.dbu))
                for pin in padframe.RAILS:
                    if item['nets'][pin] != net:
                        continue
                    for layer, rectangle in padframe.pin_shapes(lef, item['cell'], pin):
                        if layer == target_layer:
                            rails.insert(db.Box(*[round(value / layout.dbu) for value in rectangle]).transformed(transform))
            route = db.Region(decap.begin_shapes_rec(layout.layer(target_layer, 0)))
            self.assertFalse((route & rails).is_empty(), net + ' decap route missed the enlarged pad ring')

    def test_invalid_core_size(self):
        for value in (0, 639, 640.5, True):
            with self.assertRaisesRegex(ValueError, 'core opening'):
                padframe.assemble(args.pdk.resolve(), args.work.resolve() / 'invalid', core_size=value)

    def test_common_ground_is_a_physical_connection(self):
        work = args.work.resolve() / 'common_ground'
        result = padframe.assemble(args.pdk.resolve(), work, common_ground=True)
        self.assertTrue(result['common_ground'])
        self.assertEqual(len(result['ports']), 43)
        self.assertNotIn('iovss', result['ports'])
        self.assertEqual(len(result['ground_straps']), 4)
        for instance in result['placements']:
            self.assertEqual(instance['nets']['vss'], 'vss')
            self.assertEqual(instance['nets']['iovss'], 'vss')
        layout = db.Layout()
        layout.read(str(work / 'repeater_padframe.gds'))
        metal = db.Region(layout.top_cell().shapes(layout.layer(30, 0)))
        for strap in result['ground_straps']:
            box = db.Box(*[round(value / layout.dbu) for value in strap['bbox_um']])
            self.assertTrue((db.Region(box) - metal).is_empty())
        self.assertFalse(result['full_1000base_t_chip'])

    def test_real_pads_and_esd(self):
        result = padframe.assemble(args.pdk.resolve(), args.work.resolve())
        self.assertEqual(result["cells"]["sg13g2_IOPadAnalog"], 16)
        self.assertEqual(result["cells"]["sg13g2_Corner"], 4)
        self.assertEqual(len(result["placements"]), 36)
        self.assertEqual(len(result["ports"]), 44)
        self.assertEqual(result["abutment_rail_components"], dict.fromkeys(padframe.RAILS, 1))
        self.assertFalse(result["full_1000base_t_chip"])
        self.assertTrue(result["seal_ring_present"])
        self.assertEqual(result["sealring"]["ring_holes"], 1)
        self.assertEqual(result["decoupling"]["count"], 32)
        self.assertFalse(result["lvs_adapter"]["parameters_changed"])
        self.assertEqual(len(result['mdi_paths']), 16)
        for path in result['mdi_paths']:
            self.assertEqual(path['transmit_pin'], 'pad')
            self.assertEqual(path['receive_pin'], 'padres')
            self.assertEqual(path['transmit_net'], path['name'])
            self.assertEqual(path['receive_net'], path['name'] + '_core')
            self.assertNotEqual(path['transmit_net'], path['receive_net'])
        adapted = (args.work / "io_klayout.cdl").read_text()
        # Cadence globals must reach primitive resistor bulk annotations too.
        self.assertNotRegex(adapted, r"(?i)\$SUB=sub!")
        self.assertIn(".SUBCKT sg13g2_RCClampResistor pin1 pin2 CPPHDL_SUBSTRATE", adapted)
        self.assertNotIn(".global", adapted.lower())
        library = (args.pdk / "ihp-sg13g2/libs.ref/sg13g2_io/cdl/sg13g2_io.cdl").read_text()
        analog = re.search(r"\.SUBCKT sg13g2_IOPadAnalog .*?\.ENDS", library, re.S)[0]
        for device in ("sg13g2_Clamp_P20N0D", "sg13g2_Clamp_N20N0D", "sg13g2_SecondaryProtection"):
            self.assertIn(device, analog)
        for cell in ("sg13g2_IOPadVdd", "sg13g2_IOPadIOVdd"):
            supply = re.search(r"\.SUBCKT " + cell + r" .*?\.ENDS", library, re.S)[0]
            self.assertIn("sg13g2_RCClampInverter", supply)
            self.assertIn("sg13g2_Clamp_N43N43D4R", supply)
        self.assertEqual(json.loads((args.work / "interface.json").read_text()), result["ports"])
        # The fill proxy must reserve the complete physical macro extents while
        # leaving every original pad/seal instance intact. It must not expand
        # the repeated I/O device hierarchy into the temporary fill layout.
        from fill_padframe import prepare_proxy
        original = db.Layout()
        original.read(str(args.work / "repeater_padframe.gds"))
        top = original.top_cell()
        before = sum(1 for _ in top.each_inst())
        proxy = db.Layout()
        fill_top, excluded = prepare_proxy(original, top, proxy)
        self.assertEqual(before, 38)
        self.assertEqual(sum(1 for _ in top.each_inst()), before)
        self.assertEqual(len(excluded), 37)
        self.assertEqual(sum(1 for _ in fill_top.each_inst()), 1)
        self.assertIsNone(proxy.cell("sg13g2_IOPadAnalog"))
        self.assertIsNone(proxy.cell("sg13g2_decap_8"))
        row = original.cell("RepeaterCoreDecoupling")
        self.assertEqual(sum(1 for _ in row.each_inst()), 32)
        cdl = (args.work / "padframe.cdl").read_text()
        self.assertEqual(len(re.findall(r'^Xdecap\d+ vdd vss sg13g2_decap_8$', cdl, re.M)), 32)
        feol = db.Region(fill_top.shapes(proxy.layer(1, 23)))
        metal = db.Region(fill_top.shapes(proxy.layer(160, 0)))
        for instance in top.each_inst():
            if instance.cell.name.startswith("sg13g2_"):
                box = instance.cell.bbox().transformed(instance.trans)
                self.assertTrue((db.Region(box.enlarged(round(5 / original.dbu))) - feol).is_empty())
                self.assertTrue((db.Region(box.enlarged(round(10 / original.dbu))) - metal).is_empty())

    def test_missing_pad_is_a_physical_gap(self):
        plan = padframe.pad_plan()
        plan["south"].pop()
        with patch.object(padframe, "pad_plan", return_value=plan):
            with self.assertRaisesRegex(ValueError, "gap"):
                padframe.assemble(args.pdk.resolve(), args.work.resolve() / "negative")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pdk", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args, extra = parser.parse_known_args()
    unittest.main(argv=[__file__, *extra])
