"""Faults must fail both the circuit contract and reference comparison."""
import copy
import math
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from run import check, compare, read_trace, run
from inventory import classify


class Contracts(unittest.TestCase):
    def setUp(self):
        self.trace = [[i * 1e-9, math.exp(-i / 1000)] for i in range(2001)]

    def test_independent_and_comparison(self):
        check('linear_charge', self.trace, 1)
        compare(self.trace, self.trace)
        wrong = [[t, v + .1] for t, v in self.trace]
        with self.assertRaises(ValueError):
            check('linear_charge', wrong, 1)
        with self.assertRaises(ValueError):
            compare(self.trace, wrong)

    def test_missing_end_or_coarse_grid(self):
        for rows in (self.trace[:-1], self.trace[::2]):
            with self.assertRaises(ValueError):
                check('linear_charge', rows, 1)

    def test_reference_only_spike(self):
        # Sampling only at native times would miss this reference-only spike.
        reference = copy.deepcopy(self.trace)
        reference.insert(501, [500.5e-9, .9])
        with self.assertRaises(ValueError):
            compare(self.trace, reference)

    def test_nonfinite_and_duplicate_time(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.csv'
            for text in ('time_s,out\n0,nan\n', 'time_s,out\n0,1\n0,1\n'):
                path.write_text(text)
                with self.assertRaises(ValueError):
                    read_trace(path)

    def test_dc_sign_and_nonlinear_equation(self):
        check('linear_dc', [[0, -.25]], -.5)
        check('nonlinear_dc', [[0, .5]], 1.125)
        with self.assertRaises(ValueError):
            check('nonlinear_dc', [[0, .5]], 1)

    def test_native_failure_stops_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            native, reference = work / 'native', work / 'reference'
            native.write_text('#!/bin/sh\nexit 7\n')
            reference.write_text('#!/bin/sh\ntouch "' + str(work / 'reference-ran') + '"\n')
            native.chmod(0o700)
            reference.chmod(0o700)
            with self.assertRaisesRegex(ValueError, 'cpphdl failed'):
                run(SimpleNamespace(work=work / 'results', bench=native, ngspice=reference))
            self.assertFalse((work / 'reference-ran').exists())

    def test_inventory_excludes_non_simulation_commands(self):
        test = dict(name='analog.1g.ihp_receive_conversion.cpp',
                    command=['python', 'case.py', '--ngspice', '/bin/ngspice', '--flow', 'cpp'])
        self.assertIsNone(classify(test))
        test['command'][-1] = 'veriloga'
        self.assertEqual(classify(test)['domain'], 'IHPSG13G2')
        test['command'] += ['--emit-only']
        self.assertIsNone(classify(test))
        test['command'] = ['cmake', '-DNGSPICE=/bin/ngspice', '-DFLOW=veriloga', '-P', 'RunCase.cmake']
        self.assertIsNotNone(classify(test))
        test['command'][2] = '-DFLOW=cpp'
        self.assertIsNone(classify(test))


if __name__ == '__main__':
    unittest.main()
