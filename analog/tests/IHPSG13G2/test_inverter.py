"""Reject broken cell lowering, stuck outputs and invalid comparison evidence."""
import copy
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import inverter


CDL = '''.SUBCKT sg13g2_inv_1 Y A VDD VSS
*.PININFO A:I Y:O VDD:B VSS:B
MN0 Y A VSS VSS sg13_lv_nmos m=1 w=740n l=130n ng=1
MP0 Y A VDD VDD sg13_lv_pmos m=1 w=1.12u l=130n ng=1
.ENDS
'''


class InverterContract(unittest.TestCase):
    def test_cdl_topology_and_geometry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'cell.cdl'
            path.write_text(CDL)
            geometry, text = inverter.cell(path)
            self.assertAlmostEqual(geometry['n']['width'], .74e-6)
            self.assertAlmostEqual(geometry['p']['width'], 1.12e-6)
            self.assertIn('XN0 Y A VSS VSS sg13_lv_nmos m=1 w=740n l=130n ng=1', text)
            for bad in (CDL.replace('Y A VDD VSS', 'A Y VDD VSS'),
                        CDL.replace('Y A VSS VSS sg13', 'Y A VDD VSS sg13'),
                        CDL.replace('ng=1', 'ng=2'), CDL.replace('m=1', 'm=2'),
                        CDL.replace('l=130n', 'l=130n rfmode=1'),
                        CDL.replace('MP0', 'XP0'), CDL.replace('.ENDS', 'R0 Y VSS 1000\n.ENDS'),
                        CDL.replace('MP0 Y A VDD VDD sg13_lv_pmos m=1 w=1.12u l=130n ng=1\n', '')):
                path.write_text(bad)
                with self.assertRaises(ValueError):
                    inverter.cell(path)

    def test_waveform_contract(self):
        rows = [[i * 1e-12, 1.2 - inverter.stimulus(max(0, i * 1e-12 - .01e-9), 1.2),
                 inverter.stimulus(i * 1e-12, 1.2)] for i in range(3001)]
        inverter.check(rows, 'charge', 1.2)
        inverter.compare(rows, rows, 'charge')
        for kind in ('stuck', 'stimulus', 'truncated', 'oscillation', 'undersampled'):
            bad = copy.deepcopy(rows)
            if kind == 'stuck':
                for row in bad:
                    row[1] = 1.2
            elif kind == 'stimulus':
                bad[100][2] += .1
            elif kind == 'truncated':
                bad.pop()
            elif kind == 'oscillation':
                bad[800][1] = 1.2
            else:
                del bad[500]
            with self.assertRaises(ValueError, msg=kind):
                inverter.check(bad, 'charge', 1.2)
        bad = copy.deepcopy(rows)
        bad[800][1] += .0001
        with self.assertRaises(ValueError):
            inverter.compare(rows, bad, 'charge')

    def test_dc_and_trace_contract(self):
        rows = [[i * .06, 1.2 if i < 10 else .6 if i == 10 else 0] for i in range(21)]
        inverter.check(rows, 'dc', 1.2)
        inverter.compare(rows, rows, 'dc')
        with self.assertRaises(ValueError):
            inverter.check(rows[:-1], 'dc', 1.2)
        bad = copy.deepcopy(rows)
        bad[0][1] = 0
        with self.assertRaises(ValueError):
            inverter.check(bad, 'dc', 1.2)
        bad = copy.deepcopy(rows)
        bad[10][1] += 1e-6
        with self.assertRaises(ValueError):
            inverter.compare(rows, bad, 'dc')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.csv'
            for text in ('', 'input_V,output_V\n', 'input_V,output_V\n0,nan\n',
                         'input_V,output_V\n0,1\n0,1\n', 'wrong,headers\n0,1\n'):
                path.write_text(text)
                with self.assertRaises(ValueError):
                    inverter.trace(path, 'dc')

    def test_native_failure_prevents_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cdl = root / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl'
            cdl.parent.mkdir(parents=True)
            cdl.write_text(CDL)
            native, reference = root / 'native', root / 'reference'
            native.write_text('#!/bin/sh\nexit 1\n')
            reference.write_text('#!/bin/sh\ntouch "' + str(root / 'reference-ran') + '"\n')
            native.chmod(0o755)
            reference.chmod(0o755)
            bundle = dict(module='PSP103VA', model={}, instance={}, binary=str(native), input_sha256={})
            args = SimpleNamespace(work=root / 'results', bench=native, ngspice=reference, pdk=root)
            with patch.object(inverter.parameters, 'psp', return_value=bundle):
                with self.assertRaises(ValueError):
                    inverter.run(args)
            self.assertFalse((root / 'reference-ran').exists())


if __name__ == '__main__':
    unittest.main()
