"""The receive checker must reject acquisition faults and truncated trajectories."""
import copy
import unittest

from receive_frontend import contract, dc_transfer, verify


def settled_trace():
    rows = []
    first = dc_transfer(1.53, 1.77, 3.29)[1]
    second = dc_transfer(1.71, 1.59, 3.29)[1]
    for tick in range(2001):
        time = tick * 10e-12
        level = -2 if time < 12e-9 else 1
        plus, minus = 1.65 + .06 * level, 1.65 - .06 * level
        drain, output = dc_transfer(plus, minus, 3.29)
        held = 1.65 if time < 11e-9 else first if time < 15e-9 else second
        rows.append([time, plus, minus, drain, output, 3.29, 1.19, held])
    return rows


class ReceiveContract(unittest.TestCase):
    def test_independent_dc_and_retained_samples_satisfy_delivery(self):
        rows = settled_trace()
        verify(contract(rows, 1, 10), False)
        # SPICE UIC starts at a small positive accepted time, not necessarily 0.
        rows[0][0] = .1e-12
        verify(contract(rows, 1, 10), False)

    def test_thirty_millivolt_acquisition_fault_is_rejected(self):
        rows = settled_trace()
        for row in rows:
            row[-1] += .03
        result = contract(rows, 1, 10)
        self.assertFalse(result['checks']['acquisition'])
        with self.assertRaisesRegex(ValueError, 'contract failed'):
            verify(result, False)

    def test_missing_initial_final_nonfinite_and_sparse_samples_are_rejected(self):
        baseline = settled_trace()
        corruptions = []
        rows = copy.deepcopy(baseline)
        rows[0][0] = 11e-12
        corruptions += [rows, baseline[:-1], baseline[::2]]
        rows = copy.deepcopy(baseline)
        rows[100][-1] = float('nan')
        corruptions.append(rows)
        for rows in corruptions:
            with self.subTest(first=rows[0][0], last=rows[-1][0]), self.assertRaises(ValueError):
                contract(rows, 1, 10)

    def test_overload_must_expose_a_delivery_failure(self):
        result = contract(settled_trace(), 1, 10)
        with self.assertRaisesRegex(ValueError, 'overload did not expose'):
            verify(result, True)


if __name__ == '__main__':
    unittest.main()
