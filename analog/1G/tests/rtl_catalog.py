"""Regression for shared classes exported under different clock interfaces."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from generate_rtl import merge_export


class ClockCatalog(unittest.TestCase):
    def test_clock_variants_preserve_both_hierarchies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = root / 'out'
            out.mkdir()
            canonical = set()
            for top, clock in [('First', 'clk'), ('Second', 'pcs_clk')]:
                source = root / top
                source.mkdir()
                (source / 'Predef_pkg.sv').write_text('package Predef_pkg; endpackage\n')
                (source / 'Leaf.sv').write_text(
                    f'module Leaf(input {clock}, input d, output reg q);\n'
                    f'always @(posedge {clock}) q <= d;\nendmodule\n')
                (source / (top + '.sv')).write_text(
                    f'module {top}(input {clock}, input d, output q);\n'
                    f'Leaf leaf (.{clock}({clock}), .d(d), .q(q));\nendmodule\n')
                merge_export(source, out, top.lower(), canonical)
            self.assertIn('first__Leaf leaf (.clk(clk)', (out / 'First.sv').read_text())
            self.assertIn('second__Leaf leaf (.pcs_clk(pcs_clk)', (out / 'Second.sv').read_text())
            self.assertIn('posedge clk', (out / 'first__Leaf.sv').read_text())
            self.assertIn('posedge pcs_clk', (out / 'second__Leaf.sv').read_text())
            self.assertFalse((out / 'Leaf.sv').exists())
            self.assertNotIn('second__Leaf', (out / 'first.f').read_text())
            self.assertEqual({'First', 'Second'}, canonical)
            # Re-exporting an independent root must not replace its first
            # clock specialization, even when another include exposes it.
            first = (out / 'First.sv').read_bytes()
            merge_export(root / 'First', out, 'third', canonical)
            self.assertEqual(first, (out / 'First.sv').read_bytes())
            self.assertIn('third__Leaf leaf', (out / 'third__First.sv').read_text())
            (root / 'First/Predef_pkg.sv').write_text('package incompatible; endpackage\n')
            with self.assertRaisesRegex(ValueError, 'incompatible runtime packages'):
                merge_export(root / 'First', out, 'fourth', canonical)


if __name__ == '__main__':
    unittest.main()
