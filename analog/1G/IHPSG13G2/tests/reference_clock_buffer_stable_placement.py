#!/usr/bin/env python3
"""Preserve oscillator order and every component during local buffer relocation."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from reference_clock_buffer_stable_placement import stable_buffered_order


class Cell:
    def __init__(self, name, width=20):
        self.name, self.extent = name, width

    def bbox(self):
        return self

    def width(self):
        return self.extent


class BufferPlacement(unittest.TestCase):
    def setUp(self):
        self.devices = [dict(role=role, a='PRED' if i < 2 else 'BUFFERED') for i, role in
            enumerate(('predriver_n', 'predriver_p', 'driver_n', 'driver_p'))]
        self.oscillator = ['RingA', 'RealizedMOS0', 'RealizedMOS1', 'RingB', 'TuningCap']
        self.support = ['Bias', 'RealizedMOS2', 'StorageA', 'RealizedMOS3', 'StorageB', 'StorageC', 'Translator']
        connected = {'RealizedMOS0', 'RealizedMOS1', 'RealizedMOS2', 'RealizedMOS3', 'StorageA', 'StorageB', 'StorageC'}
        self.parts = [(Cell(name), {}, {'A': 'PRED' if name in connected else 'OTHER'}, 'fixture')
                      for name in self.oscillator + self.support]

    def baseline(self, parts, dbu):
        return list(range(len(parts))), dict(groups=dict(
            oscillator=dict(component_order=self.oscillator),
            support=dict(component_order=self.support)))

    def order(self):
        return stable_buffered_order(self.devices, self.baseline)(self.parts, 1)

    def test_preserves_existing_orders_and_all_component_identities(self):
        ordered, result = self.order()
        actual = result['groups']['oscillator']['component_order']
        self.assertEqual([name for name in actual if name in self.oscillator], self.oscillator)
        self.assertEqual(result['groups']['support']['component_order'], ['Bias', 'Translator'])
        self.assertEqual(sorted(ordered), list(range(len(self.parts))))
        self.assertEqual(len(set(ordered)), len(self.parts))
        self.assertTrue({'RealizedMOS2', 'RealizedMOS3'} <= set(actual))
        self.assertLess(actual.index('RealizedMOS1'), actual.index('RealizedMOS2'))
        self.assertLess(actual.index('RealizedMOS3'), actual.index('RingB'))

    def test_duplicate_physical_identity(self):
        self.parts[-1] = (Cell('Bias'), {}, {}, 'fixture')
        with self.assertRaisesRegex(ValueError, 'duplicate physical'):
            self.order()

    def test_extra_buffer_load_needs_review(self):
        self.parts.append((Cell('NewLoad'), {}, {'A': 'PRED'}, 'fixture'))
        with self.assertRaisesRegex(ValueError, 'unreviewed clock buffer'):
            self.order()

    def test_missing_transistor_role(self):
        self.devices.pop()
        with self.assertRaisesRegex(ValueError, 'transistor roles'):
            self.order()

    def test_disconnected_predriver(self):
        self.devices[1]['a'] = 'OTHER'
        with self.assertRaisesRegex(ValueError, 'outputs are disconnected'):
            self.order()

    def test_row_overflow(self):
        self.parts[0][0].extent = 1800
        with self.assertRaisesRegex(ValueError, 'width budget'):
            self.order()


if __name__ == '__main__':
    unittest.main()
