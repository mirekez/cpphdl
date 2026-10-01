import importlib.util
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('netlist_driver', Path(__file__).resolve().parents[2] / 'tools/cpphdl-netlist.py')
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class DriverTests(unittest.TestCase):
    def test_dependency_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            top = root / 'A.sv'
            middle = root / 'M.sv'
            base = root / 'Z.sv'
            top.write_text('import Middle::*; module A; endmodule')
            middle.write_text('package Middle; typedef Base::word word; endpackage')
            base.write_text('package Base; typedef logic [7:0] word; endpackage')
            self.assertEqual(driver.ordered_sources(root), [base, middle, top])

    def test_cycle_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'A.sv').write_text('package A; import B::*; endpackage')
            (root / 'B.sv').write_text('package B; import A::*; endpackage')
            with self.assertRaisesRegex(RuntimeError, 'Cyclic'):
                driver.ordered_sources(root)

    def test_duplicates_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ['A', 'B']:
                (root / (name + '.sv')).write_text('package Duplicate; endpackage')
            with self.assertRaisesRegex(RuntimeError, 'Duplicate'):
                driver.ordered_sources(root)

    def test_missing_output_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'no SystemVerilog'):
                driver.ordered_sources(Path(directory))

    def test_nonempty_output_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'old-model.cc').write_text('stale')
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                driver.main(['--cpphdl', 'unused', '--top', 'Top', '--output', str(root), 'input.h'])

    def test_logged_assertion_is_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'input.h'
            source.write_text('')
            output = root / 'model'

            def failed_converter(command, **kwargs):
                kwargs['stdout'].write('ASSERT at Expr.cpp:1\n')
                return mock.Mock(returncode=0)

            with mock.patch.object(driver, 'executable', side_effect=lambda name: name), \
                 mock.patch.object(driver.subprocess, 'run', side_effect=failed_converter) as run, \
                 contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(driver.main(['--cpphdl', 'cpphdl', '--top', 'Top',
                                              '--output', str(output), str(source)]), 1)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(json.loads((output / 'manifest.json').read_text())['status'], 'failed')


if __name__ == '__main__':
    unittest.main()
