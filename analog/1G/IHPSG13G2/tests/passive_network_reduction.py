#!/usr/bin/env python3
"""Compare mesh terminal currents before and after exact node elimination."""
from pathlib import Path
import random
import runpy
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from passive_network_reduction import reduce_resistors

# Independent dense Gaussian-elimination oracle already used by wire tests.
currents = runpy.run_path(str(Path(__file__).with_name('reduce_extracted_wires.py')))['currents']


class PassiveReduction(unittest.TestCase):
    def test_mesh_terminal_admittance(self):
        rng = random.Random(20261027)
        terminals = {'n0_0', 'n0_5', 'n5_0', 'n5_5', 'n2_3'}
        for _ in range(10):
            edges = []
            for x in range(6):
                for y in range(6):
                    for dx, dy in [(1, 0), (0, 1)]:
                        if x + dx < 6 and y + dy < 6:
                            edges.append((f'n{x}_{y}', f'n{x+dx}_{y+dy}', rng.uniform(.1, 100)))
            edges += [('n0_0', 'dead', 1), ('n0_0', 'n1_0', 2)]
            reduced, audit = reduce_resistors(edges, terminals)
            self.assertLess(len(reduced), len(edges))
            self.assertGreater(audit['eliminated_unloaded_nodes'], 0)
            # Basis excitations compare the complete retained-terminal matrix.
            for driven in terminals:
                values = {n: float(n == driven) for n in terminals}
                before, after = currents(edges, values), currents(reduced, values)
                for node in terminals:
                    self.assertAlmostEqual(before[node], after[node], delta=1e-11)

    def test_invalid_edges(self):
        for edge in [('a', 'a', 1), ('a', 'b', 0), ('a', 'b', -1), ('a', 'b', float('nan'))]:
            with self.assertRaises(ValueError):
                reduce_resistors([edge], {'a', 'b'})


if __name__ == '__main__':
    unittest.main()
