#!/usr/bin/env python3
"""Ensure a timing ECO cannot silently alter logic or pin connectivity."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sizing_equivalence import compare

NETLIST = '''module top(a, b, y);
input a;
input b;
output y;
sg13g2_nand2_1 gate(.A(a), .B(b), .Y(y));
endmodule
'''
MODEL = '''module sg13g2_nand2_1(Y, A, B);
input A;
input B;
output Y;
assign Y = ~(A & B);
endmodule
'''


class SizingProof(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.a, self.b, self.models = [Path(temporary.name) / name
                                     for name in ('before.v', 'after.v', 'cells.v')]
        self.a.write_text(NETLIST)
        self.b.write_text(NETLIST.replace('nand2_1', 'nand2_2'))
        self.models.write_text(MODEL + MODEL.replace('nand2_1', 'nand2_2'))

    def test_identical_drive_variant(self):
        result = compare(self.a, self.b, self.models)
        self.assertEqual(result['resized_cells'], 1)
        self.assertFalse(result['timing_qualified'])

    def test_changed_function_or_missing_model(self):
        for text in (MODEL, MODEL + MODEL.replace('nand2_1', 'nand2_2').replace('&', '|')):
            self.models.write_text(text)
            with self.assertRaises(ValueError): compare(self.a, self.b, self.models)

    def test_delay_replacement_preserves_only_logic(self):
        netlist = NETLIST.replace('sg13g2_nand2_1', 'sg13g2_dlygate4sd3_1')
        netlist = netlist.replace('.A(a), .B(b), .Y(y)', '.A(a), .X(y)')
        model = 'module sg13g2_dlygate4sd3_1(A, X); input A; output X; assign X = A; endmodule\n'
        self.a.write_text(netlist)
        self.b.write_text(netlist.replace('sg13g2_dlygate4sd3_1', 'sg13g2_buf_1'))
        self.models.write_text(model + model.replace('sg13g2_dlygate4sd3_1', 'sg13g2_buf_1'))
        result = compare(self.a, self.b, self.models)
        self.assertEqual(result['resized_cells'], 1)
        self.assertFalse(result['timing_qualified'])

    def test_changed_connections_cells_or_ports(self):
        for text in (NETLIST.replace('.A(a)', '.A(b)'),
                     NETLIST.replace('gate(', 'new_gate('),
                     NETLIST.replace('output y;', 'input y;')):
            self.b.write_text(text)
            with self.assertRaises(ValueError): compare(self.a, self.b, self.models)


if __name__ == '__main__':
    unittest.main()
