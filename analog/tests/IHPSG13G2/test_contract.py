"""Fault checks for the bounded process frontend and comparison acceptance."""
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import run
from run import parameters


class Contract(unittest.TestCase):
    def test_expression(self):
        self.assertEqual(parameters.expression("'2*w/l + 1'", dict(w=3, l=2)), 4)
        self.assertAlmostEqual(parameters.expression('0.35u', {}), .35e-6)
        self.assertEqual(parameters.expression('2MEG', {}), 2e6)
        for text in ('__import__("os")', '1e999', '[1]', 'w.x', '1//2'):
            with self.assertRaises((ValueError, SyntaxError)): parameters.expression(text, {})
        with self.assertRaises(KeyError): parameters.expression('missing', {})
        with self.assertRaises(ValueError): parameters.assignments('w=1 w=2', {})
        with self.assertRaises(ValueError): parameters.assignments('w=1 leftover', {})

    def test_bounds(self):
        for values in (dict(corner='mos_mc'), dict(voltage='fake'), dict(width=float('nan')),
                       dict(voltage='hv', length=.13e-6)):
            with self.assertRaises(ValueError): parameters.psp('/absent', **values)

    def test_parser_and_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'trace.csv'
            for data in ('gate_V,drain_V,current_A\n', 'bad,header,columns\n0,1,0\n',
                         'gate_V,drain_V,current_A\n0,nan,0\n',
                         'gate_V,drain_V,current_A\n0,1,0\n0,1,0\n'):
                path.write_text(data)
                with self.assertRaises(ValueError): run.trace(path, 'dc')
        rows = [[i*.3, 1.2-i*.1, i*.0001] for i in range(5)]
        run.check(rows, 'dc', 'n')
        run.compare(rows, rows, 'dc')
        with self.assertRaises(ValueError): run.check(rows[:-1], 'dc', 'n')
        broken = [r[:] for r in rows]
        broken[2][1] += 1e-4
        with self.assertRaises(ValueError): run.compare(rows, broken, 'dc')
        with self.assertRaises(ValueError): run.check(broken, 'dc', 'n')

    def test_reference_uses_original_wrapper(self):
        text = run.deck(dict(polarity='n', voltage='lv', width=1e-6, length=.13e-6, corner='mos_tt'), 'charge', 27)
        self.assertIn('.lib cornerMOSlv.lib mos_tt', text)
        self.assertIn('sg13_lv_nmos', text)
        self.assertNotIn('.model', text)
        self.assertIn('tran .5p 2n', text)

    def test_native_failure_stops_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native, reference = root/'native', root/'reference'
            native.write_text('#!/bin/sh\nexit 1\n')
            reference.write_text('#!/bin/sh\ntouch "'+str(root/'reference-was-run')+'"\n')
            native.chmod(0o755)
            reference.chmod(0o755)
            bundle = dict(module='PSP103VA', model={}, instance={}, binary=str(native), input_sha256={})
            args = SimpleNamespace(work=root/'results', bench=native, ngspice=reference, pdk=root)
            with patch.object(parameters, 'psp', return_value=bundle):
                with self.assertRaises(ValueError): run.run(args)
            self.assertFalse((root/'reference-was-run').exists())


if __name__ == '__main__': unittest.main()
