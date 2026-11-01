"""Reject altered sampler charge, transistor topology and parent ownership."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receive_sampler_layout import circuit

parser = argparse.ArgumentParser()
parser.add_argument('--exporter', type=Path, required=True)
args, remaining = parser.parse_known_args()


class SamplerGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        subprocess.run([args.exporter.resolve(), cls.directory], check=True, timeout=30)
        cls.source = (cls.directory / 'sampler.va').read_text()
        cls.manifest = json.loads((cls.directory / 'sampler.json').read_text())

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def check(self, source=None, manifest=None):
        p = self.directory
        (p / 'test.va').write_text(self.source if source is None else source)
        (p / 'test.json').write_text(json.dumps(self.manifest if manifest is None else manifest))
        return circuit(p / 'test.va', p / 'test.json')

    def test_actual_cold_slice(self):
        plan = self.check()
        self.assertEqual(len(plan['devices']), 6)
        self.assertEqual(len(plan['capacitors']), 5)
        self.assertEqual(sorted(r['element'] for r in plan['devices'] + plan['capacitors']),
                         plan['owned_contributions'])

    def test_wrong_dummy_clock_rejected(self):
        inverse = self.manifest['clock_inverse']
        old = f'I(OUT, OUT) <+ cpphdl_channel(V({inverse}, OUT)'
        self.assertEqual(self.source.count(old), 1)
        with self.assertRaises(ValueError):
            self.check(self.source.replace(old, 'I(OUT, OUT) <+ cpphdl_channel(V(CLOCK, OUT)'))

    def test_main_channel_reversal_rejected(self):
        old = 'I(IN, OUT) <+ cpphdl_channel(V(CLOCK, OUT), V(IN, OUT)'
        self.assertEqual(self.source.count(old), 1)
        with self.assertRaises(ValueError):
            self.check(self.source.replace(old, 'I(OUT, IN) <+ cpphdl_channel(V(CLOCK, IN), V(OUT, IN)'))

    def test_hold_storage_and_precharge_rejected(self):
        old = '1.5000000000000001e-12 * ddt(V(OUT, VSS))'
        self.assertEqual(self.source.count(old), 1)
        with self.assertRaises(ValueError):
            self.check(self.source.replace(old, '1.5e-13 * ddt(V(OUT, VSS))'))
        with self.assertRaises(ValueError):
            self.check(self.source.replace('Bench initial voltage = 0.0 V.', 'Bench initial voltage = 1.65 V.', 1))

    def test_duplicate_parent_contribution_rejected(self):
        data = json.loads(json.dumps(self.manifest))
        data['selected_contributions'][1] = data['selected_contributions'][0]
        with self.assertRaises(ValueError):
            self.check(manifest=data)

    def test_unowned_law_rejected(self):
        with self.assertRaises(ValueError):
            self.check(self.source.replace('    end\nendmodule',
                '        I(OUT, VSS) <+ V(OUT, VSS) / 1000;\n    end\nendmodule'))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
