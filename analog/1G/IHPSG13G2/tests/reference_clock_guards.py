"""Reject invalid clock waveforms and unreviewed process-device bindings."""
import argparse
import copy
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from reference_clock_process import process_deck
from reference_clock_contract import measure


class ClockContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Deterministic valid rails and exact 2:1 clocks, with a physical
        # propagation offset. Mutations below must fail independently.
        cls.rows = []
        for i in range(100001):
            time = i * 20e-12
            low = 1.2 if i % 200 < 100 else 0
            out = 3.3 if (i - 15) % 200 < 100 else 0
            divided = 1.2 if (i - 10) % 400 < 200 else 0
            reference = 1.2 if i % 400 < 200 else 0
            cls.rows.append([time, low, out, 3.3 - out, divided, reference,
                             .7, 1.19, 3.29, 1.5, 0, 0, 1.2 - low, low])

    def test_valid_fixed_delay_is_not_jitter(self):
        self.assertEqual(measure(self.rows)['status'], 'pass')

    def test_supply_collapse(self):
        rows = copy.deepcopy(self.rows)
        rows[85000][7] = 1.09
        self.assertFalse(measure(rows)['checks']['supplies'])

    def test_lost_output_cycle(self):
        rows = copy.deepcopy(self.rows)
        for row in rows[85000:85200]:
            row[2] = 0
        self.assertFalse(measure(rows)['checks']['frequency'])

    def test_missing_tail(self):
        with self.assertRaises(ValueError):
            measure(self.rows[:-1])

    def test_unresolved_interval(self):
        with self.assertRaises(ValueError):
            measure(self.rows[:50000] + self.rows[50002:])


def binding_guards(bench):
    with tempfile.TemporaryDirectory(prefix='clock-bindings-') as name:
        work = Path(name)
        subprocess.run([str(bench.resolve()), name, 'export', '0', '0', '0'], check=True, timeout=60)
        model, original = (work / 'design.va').read_text(), (work / 'bench.cir').read_text()
        deck, result = process_deck(model, original, Path('/models'))
        if len(result['geometry']) != 230 or 'cpphdl_channel' in deck or 'design.osdi' in deck:
            raise ValueError('clock binding inventory failure')
        capacitors = [line for line in original.splitlines() if line.startswith('Cdevice')]
        if not capacitors or any(line not in deck.splitlines() for line in capacitors):
            raise ValueError('clock charge storage was lost during process binding')
        changed_resistance, changed_count = re.subn(r'/ 10000\.0;', '/ 1000000000.0;', model, count=1)
        if changed_count != 1:
            raise ValueError('missing pump resistance for the negative binding case')
        for changed in (model.replace('0.002, 0.34999999999999998', '0.0021, 0.34999999999999998', 1),
                        model.replace('    analog begin\n', '    analog begin\n        I(n1, n0) <+ 1;\n', 1),
                        changed_resistance):
            try:
                process_deck(changed, original, Path('/models'))
            except ValueError:
                continue
            raise ValueError('unreviewed clock device law was accepted')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    args = parser.parse_args()
    binding_guards(args.bench)
    unittest.main(argv=[sys.argv[0]])
