#!/usr/bin/env python3
"""Reject missing startup, unsafe devices, lost bias and conflicting proof hashes."""
import math
from pathlib import Path
import tempfile
import unittest

from transmit_translator_replica import measure, merge_hashes


class ConnectedTranslatorGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.storage = tempfile.TemporaryDirectory()
        cls.work = Path(cls.storage.name)
        cls.trace, cls.bias = ['time Y Y_N A VDD VLOGIC COMMAND EXPOSURE\n'], ['time BIAS\n']
        for index in range(68097):
            time = index * 31.25e-12
            high = int(max(0, (time - 2e-6 - .1e-9) / 8e-9)) % 2
            cls.trace.append(' '.join(map(str, (time, 3.29 * high, 3.29 * (1 - high),
                1.2 * high, 3.29, 1.2, 1.2 * high, 1.2))) + '\n')
            cls.bias.append(f'{time} {1.8 * (1 - math.exp(-time / 50e-9))}\n')

    @classmethod
    def tearDownClass(cls):
        cls.storage.cleanup()

    def setUp(self):
        (self.work / 'trace.dat').write_text(''.join(self.trace))
        (self.work / 'bias.dat').write_text(''.join(self.bias))

    def test_independent_connected_contract(self):
        self.assertEqual(measure(self.work)['status'], 'pass')

    def test_missing_startup(self):
        (self.work / 'trace.dat').write_text(self.trace[0] + ''.join(self.trace[100:]))
        with self.assertRaisesRegex(ValueError, 'startup trace'):
            measure(self.work)

    def test_missing_bias_observation(self):
        (self.work / 'bias.dat').write_text(''.join(self.bias[:-1]))
        with self.assertRaisesRegex(ValueError, 'bias observations'):
            measure(self.work)

    def test_unsafe_gate_exposure(self):
        trace = self.trace.copy()
        fields = trace[100].split()
        fields[-1] = '1.7'
        trace[100] = ' '.join(fields) + '\n'
        (self.work / 'trace.dat').write_text(''.join(trace))
        result = measure(self.work)
        self.assertEqual(result['status'], 'fail')
        self.assertFalse(result['checks']['lv_gate_exposure'])

    def test_lost_replica_bias(self):
        bias = self.bias.copy()
        bias[-1] = bias[-1].split()[0] + ' 0\n'
        (self.work / 'bias.dat').write_text(''.join(bias))
        result = measure(self.work)
        self.assertEqual(result['status'], 'fail')
        self.assertFalse(result['checks']['bias_headroom'])

    def test_current_dependency_cannot_hide_stale_upstream_hash(self):
        hashes = {'helper.py': 'tested-source'}
        merge_hashes(hashes, {'helper.py': 'tested-source', 'circuit.spice': 'tested-circuit'})
        with self.assertRaisesRegex(ValueError, 'inconsistent integration evidence'):
            merge_hashes(hashes, {'helper.py': 'changed-source'})
        self.assertEqual(hashes['helper.py'], 'tested-source')


if __name__ == '__main__':
    unittest.main()
