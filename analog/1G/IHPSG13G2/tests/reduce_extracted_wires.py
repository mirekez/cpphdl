#!/usr/bin/env python3
"""Check reduced-network terminal currents against independent nodal solves."""
from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extract_analog import inventory
from reduce_extracted_wires import reduce


def currents(resistors, boundary):
    nodes = sorted({n for a, b, _ in resistors for n in (a, b)} - boundary.keys())
    index = {n: i for i, n in enumerate(nodes)}
    matrix = [[0.] * (len(nodes) + 1) for _ in nodes]
    for a, b, resistance in resistors:
        conductance = 1 / resistance
        for n, other in ((a, b), (b, a)):
            if n in boundary:
                continue
            row = matrix[index[n]]
            row[index[n]] += conductance
            if other in boundary:
                row[-1] += conductance * boundary[other]
            else:
                row[index[other]] -= conductance
    for i in range(len(nodes)):
        pivot = max(range(i, len(nodes)), key=lambda j: abs(matrix[j][i]))
        matrix[i], matrix[pivot] = matrix[pivot], matrix[i]
        scale = matrix[i][i]
        if abs(scale) < 1e-18:
            raise ValueError('singular oracle circuit')
        matrix[i] = [v / scale for v in matrix[i]]
        for j in range(len(nodes)):
            if j != i:
                scale = matrix[j][i]
                matrix[j] = [a - scale * b for a, b in zip(matrix[j], matrix[i])]
    voltages = dict(boundary, **{node: matrix[i][-1] for node, i in index.items()})
    answer = dict.fromkeys(boundary, 0.)
    for a, b, resistance in resistors:
        current = (voltages[a] - voltages[b]) / resistance
        if a in answer:
            answer[a] += current
        if b in answer:
            answer[b] -= current
    return answer


class Reduction(unittest.TestCase):
    def test_terminal_currents_and_storage(self):
        rng = random.Random(20261025)
        ports = ['A', 'B', 'GND']
        for _ in range(40):
            lines = ['.subckt ExtractedAnalog A B GND',
                'Xmos A gate B GND sg13_lv_nmos w=1u l=0.13u',
                'Ccouple store gate 2f', 'Cground store GND 3f']
            edges = [('A', 'n1'), ('n1', 'n2'), ('n2', 'store'), ('A', 'store'),
                     ('store', 'n3'), ('n3', 'B'), ('n1', 'n4'), ('n4', 'gate'),
                     ('gate', 'n5'), ('n5', 'GND'), ('n2', 'dead'), ('dead', 'tail')]
            lines += [f'R{i} {a} {b} {rng.uniform(10, 1000):.17g}' for i, (a, b) in enumerate(edges)]
            lines.append('.ends')
            text = '\n'.join(lines) + '\n'
            output, audit = reduce(text, ports)
            self.assertLess(audit['reduced_resistors'], len(edges))
            self.assertGreater(audit['eliminated_dead_ends'], 0)
            self.assertGreater(audit['eliminated_star_junctions'], 0)
            before, after = inventory(text, ports), inventory(output, ports)
            self.assertEqual(before['capacitors'], after['capacitors'])
            for _ in range(5):
                values = {n: rng.uniform(-1, 1) for n in ('A', 'B', 'GND', 'gate', 'store')}
                expected = currents(before['resistors'], values)
                actual = currents(after['resistors'], values)
                for node in values:
                    self.assertAlmostEqual(expected[node], actual[node], delta=1e-12)
            with self.assertRaisesRegex(ValueError, 'unknown observation'):
                reduce(text, ports, ['missing'])


if __name__ == '__main__':
    unittest.main()
