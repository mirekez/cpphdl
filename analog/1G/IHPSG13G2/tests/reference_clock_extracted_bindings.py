"""Reject missing, disconnected or ambiguous physical clock observations."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from reference_clock_extracted_process import bind_observations


class ObservationTests(unittest.TestCase):
    def setUp(self):
        self.geometry = dict(components=[
            dict(name='Driver', connections=dict(D='N1', S='VSS')),
            dict(name='Load', connections=dict(PLUS='N1', MINUS='VSS'))])
        self.ext = 'equiv "Driver_0.D" "Load_0.PLUS"\nequiv "Driver_0.D" "wire"\n'

    def test_actual_conductor_and_port(self):
        probes, audit = bind_observations(self.geometry, dict(LOW='N1', OUT='OUT'), self.ext, {'wire', 'OUT'})
        self.assertEqual(probes, dict(LOW='wire', OUT='OUT'))
        self.assertEqual(audit['LOW']['layout_terminals'], ['Driver_0.D', 'Load_0.PLUS'])

    def test_open_branch(self):
        with self.assertRaisesRegex(ValueError, 'disconnected'):
            bind_observations(self.geometry, dict(LOW='N1'), 'equiv "Driver_0.D" "wire"\n', {'wire'})

    def test_missing_logical_net(self):
        with self.assertRaisesRegex(ValueError, 'missing'):
            bind_observations(self.geometry, dict(LOW='N2'), self.ext, {'wire'})

    def test_missing_or_ambiguous_spice_node(self):
        for nodes in [set(), {'wire', 'Driver_0.D'}]:
            with self.subTest(nodes=nodes), self.assertRaisesRegex(ValueError, 'ambiguous'):
                bind_observations(self.geometry, dict(LOW='N1'), self.ext, nodes)


if __name__ == '__main__':
    unittest.main()
