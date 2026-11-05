#!/usr/bin/env python3
"""Exercise compaction boundaries with small, independent layout fixtures."""
from pathlib import Path
import sys
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / '1G/IHPSG13G2'))
from compact_analog_rows import compact_rows


def fixture(positions=(20, 210, 400)):
    layout = db.Layout()
    layout.dbu = .001
    top = layout.create_cell('Connected')
    info = dict(components=[], escapes=[], terminals={net: {} for net in ('VDD', 'RAW', 'LOCAL')})
    for i, x in enumerate(positions):
        cell = layout.create_cell('device_' + str(i))
        cell.shapes(layout.layer(8, 0)).insert(db.Box(0, 0, 100000, 20000))
        top.insert(db.CellInstArray(cell.cell_index(), db.Trans(x * 1000, 0)))
        info['components'].append(dict(name=cell.name, origin_um=[x, 0], connections={}))
        for offset, net in ((10, 'VDD'), (20, 'RAW'), (30, 'LOCAL' if i == 0 else 'INTERNAL_' + str(i))):
            info['escapes'].append(dict(net=net, location_um=[x + offset, 10]))
            info['components'][-1]['connections'][str(offset)] = net
            top.shapes(layout.layer(50, 0)).insert(db.Box(
                (x + offset) * 1000 - 200, 0, (x + offset) * 1000 + 200, 10200))
    return layout, top, info


class CompactRows(unittest.TestCase):
    def test_preserves_primitives_and_local_only_tracks(self):
        layout, top, info = fixture()
        originals = {cell.name: cell.bbox().to_s() for cell in layout.each_cell() if cell != top}
        layout, top, info = compact_rows(layout, top, info, 200)
        self.assertEqual(len(list(layout.top_cells())), 1)
        self.assertEqual(len(list(top.each_inst())), 3)
        self.assertEqual({inst.cell.name: inst.cell.bbox().to_s() for inst in top.each_inst()}, originals)
        self.assertEqual(info['compaction']['row_bus_counts'], [3, 3, 3])
        self.assertEqual(set(info['terminals']), {'VDD', 'RAW', 'LOCAL'})
        self.assertEqual(len(info['compaction']['row_buses']['LOCAL']), 1)
        # Each VDD row crossing needs a 25-cut array plus its device tap;
        # RAW uses two cuts per row and each local-only net one.
        self.assertEqual(top.shapes(layout.layer(66, 0)).size(), 3 * 26 + 3 * 2 + 3)
        bounds = [inst.bbox() for inst in top.each_inst()]
        self.assertFalse(any(a.overlaps(b) for i, a in enumerate(bounds) for b in bounds[i + 1:]))

    def test_rejects_invalid_dimensions(self):
        for width, gap in ((float('nan'), 10), (199, 10), (200, 9), (float('inf'), 10)):
            layout, top, info = fixture()
            with self.assertRaises(ValueError):
                compact_rows(layout, top, info, width, gap)

    def test_clock_group_moves_whole_to_next_row(self):
        layout, top, info = fixture((20, 140, 260))
        info['components'][0]['connections']['20'] = 'OTHER'
        info['escapes'][1]['net'] = 'OTHER'
        _, _, info = compact_rows(layout, top, info, 250, keep_together_nets=('RAW',))
        buses = info['compaction']['row_buses']['RAW']
        self.assertEqual(len(buses), 1)
        self.assertEqual(buses[0]['row'], 1)

    def test_cannot_split_oversized_clock_group(self):
        layout, top, info = fixture()
        with self.assertRaisesRegex(ValueError, 'protected net group'):
            compact_rows(layout, top, info, 200, keep_together_nets=('RAW',))

    def test_cannot_cut_a_large_device(self):
        layout, top, info = fixture()
        layout.cell('device_0').shapes(layout.layer(8, 0)).insert(db.Box(0, 0, 250000, 20000))
        with self.assertRaisesRegex(ValueError, 'indivisible device'):
            compact_rows(layout, top, info, 200)

    def test_cannot_silently_lose_external_ports(self):
        layout, top, info = fixture()
        info['terminals']['MISSING'] = {}
        with self.assertRaisesRegex(ValueError, 'external ports'):
            compact_rows(layout, top, info, 200)

    def test_rejects_escape_below_partition(self):
        layout, top, info = fixture()
        info['escapes'][0]['location_um'][1] = -1
        with self.assertRaisesRegex(ValueError, 'routing boundary'):
            compact_rows(layout, top, info, 200)

    def test_empty_circuit_is_rejected(self):
        layout, top, info = fixture()
        info['components'] = []
        with self.assertRaisesRegex(ValueError, 'needs devices'):
            compact_rows(layout, top, info, 200)


if __name__ == '__main__':
    unittest.main()
