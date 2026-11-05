#!/usr/bin/env python3
"""Exercise real-metal interface extraction, including opened and shorted nets."""
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'out/pdf-renderer'))
import klayout.db as db
from integrate_clock_control import (TOP, abstract, connections, physical_name,
                                     placed_terminal, placements, verilog)
from clock_control_connections import extract


class ClockControl(unittest.TestCase):
    def fixture(self, directory):
        layout = db.Layout()
        layout.dbu = .001
        top = layout.create_cell(TOP)
        shared = dict(connections())
        control = {v: k for k, v in shared.items()}
        control.update({f'measured_edges_out[{i}]': f'measured_edges_out[{i}]' for i in range(14)})
        control.update({p: p for p in ('enable_in', 'fault_out', 'ready_ref_out', 'ref_clk', 'reset')})
        clock = {p: p for p in list(shared) + ['OUT']}
        names = sorted(set(clock.values()) | set(control.values()))
        xs = {name: (i + 1) * 10000 for i, name in enumerate(names)}
        metal, labels = layout.layer(67, 0), layout.layer(67, 25)
        for master, ports, y in [('ClockDeliveryMacro', clock, 10000), ('ClockControlMacro', control, 100000)]:
            cell = layout.create_cell(master)
            devices = layout.create_cell(master + '_devices')
            cell.insert(db.CellInstArray(devices.cell_index(), db.Trans()))
            top.insert(db.CellInstArray(cell.cell_index(), db.Trans()))
            for pin, net in ports.items():
                x = xs[net]
                devices.shapes(metal).insert(db.Box(x - 100, y - 100, x + 100, y + 100))
                devices.shapes(labels).insert(db.Text(pin, db.Trans(x, y)))
        for net in shared:
            x = xs[net]
            top.shapes(metal).insert(db.Box(x - 100, 10000, x + 100, 100000))
        return layout, top, metal, xs

    def test_connected_metal_matches_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            layout, _, _, _ = self.fixture(directory)
            path = Path(directory) / 'closed.gds'
            layout.write(str(path))
            result = extract(path)
            self.assertEqual(result['inter_macro_connections'], 21)
            self.assertEqual(result['distinct_interface_nets'], 41)

    def test_open_wire_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            layout, top, metal, _ = self.fixture(directory)
            next(top.shapes(metal).each()).delete()
            path = Path(directory) / 'open.gds'
            layout.write(str(path))
            with self.assertRaisesRegex(ValueError, 'open inter-macro net'):
                extract(path)

    def test_cross_net_short_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            layout, top, metal, xs = self.fixture(directory)
            a, b = xs['RAW'], xs['ADMIT']
            top.shapes(metal).insert(db.Box(min(a, b) - 100, 55000, max(a, b) + 100, 55200))
            path = Path(directory) / 'short.gds'
            layout.write(str(path))
            with self.assertRaisesRegex(ValueError, 'shorted inter-macro nets'):
                extract(path)

    def test_missing_analog_terminal_rejected(self):
        shared = dict(connections())
        with self.assertRaisesRegex(ValueError, 'interfaces differ'):
            verilog(list(shared), list(shared.values()))

    def test_bus_names_are_unambiguous(self):
        names = [physical_name(f'coarse_n_out[{i}]') for i in range(8)]
        self.assertEqual(len(set(names)), 8)
        self.assertEqual(names[7], 'coarse_n_out__7')

    def test_label_without_metal_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            layout = db.Layout()
            layout.dbu = .001
            top = layout.create_cell('Bad')
            top.shapes(layout.layer(67, 0)).insert(db.Box(0, 0, 1000, 1000))
            top.shapes(layout.layer(67, 25)).insert(db.Text('OUT', db.Trans(2000, 2000)))
            path = work / 'bad.gds'
            layout.write(str(path))
            with self.assertRaisesRegex(ValueError, 'no physical metal'):
                abstract(path, 'Bad', 'BadMacro', work)

    def test_controller_faces_and_aligns_raw_terminal(self):
        clock = dict(width_um=632.26, height_um=517.2,
                     access={'RAW': dict(point_dbu=[379580, 78560])})
        control = dict(width_um=280, height_um=280,
                       access={'pcs_clk': dict(point_dbu=[141600, 360])})
        position = placements(clock, control)
        a = placed_terminal(clock, position['delivery'], 'RAW')
        b = placed_terminal(control, position['control'], 'pcs_clk')
        self.assertEqual(a[0], b[0])
        self.assertEqual(a[1] - b[1], 198920)
        transform = db.Trans(2, False, position['control']['x'] + 280000,
                             position['control']['y'] + 280000)
        self.assertEqual(db.Point(*b), transform * db.Point(141600, 360))
        self.assertEqual(position['delivery']['y'] - position['control']['y'] - 280000, 120000)

    def test_macro_obstructions_reserve_wide_metal_clearance(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            layout = db.Layout()
            layout.dbu = .001
            top = layout.create_cell('Macro')
            top.shapes(layout.layer(67, 0)).insert(db.Box(0, 0, 10000, 400))
            top.shapes(layout.layer(67, 25)).insert(db.Text('OUT', db.Trans(500, 200)))
            top.shapes(layout.layer(50, 0)).insert(db.Box(2000, 2000, 2400, 10000))
            path = work / 'macro.gds'
            layout.write(str(path))
            lef, _, _ = abstract(path, 'Macro', 'Abstract', work)
            # The extra clearance belongs to the routing abstract only.
            self.assertIn('RECT 1.960 1.960 2.440 10.040 ;', lef)
            restored = db.Layout()
            restored.read(str(work / 'Abstract.gds'))
            metal = db.Region(restored.top_cell().begin_shapes_rec(restored.layer(50, 0)))
            self.assertEqual(metal.area(), 400 * 8000)


if __name__ == '__main__':
    unittest.main()
