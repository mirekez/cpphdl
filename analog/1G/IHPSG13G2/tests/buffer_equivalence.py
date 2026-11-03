#!/usr/bin/env python3
"""Reject invalid buffer ECOs independently of timing or routing progress."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from buffer_equivalence import compare, parse

BEFORE = '''module example (a, y);
input [1:0] a;
output y;
wire n;
wire m;
sg13g2_buf_1 buffer1 (.A(a[0]), .X(n));
sg13g2_buf_2 buffer2 (.A(n), .X(m));
sg13g2_and2_1 gate (.A(m), .B(a[1]), .X(y));
endmodule
'''
AFTER = '''module example (a, y);
input [1:0] a;
output y;
sg13g2_and2_1 gate (.A(a[0]), .B(a[1]), .X(y));
endmodule
'''


class BufferProof(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.a, self.b = (Path(directory.name) / name for name in ('before.v', 'after.v'))
        self.a.write_text(BEFORE)
        self.b.write_text(AFTER)

    def test_transparent_chain(self):
        result = compare(self.a, self.b)
        self.assertEqual(result['removed_buffers'], 2)
        self.assertFalse(result['timing_qualified'])

    def test_changed_logic_pin_and_interface(self):
        for replacement in (AFTER.replace('.A(a[0])', '.A(a[1])'),
                            AFTER.replace('and2_1', 'nand2_1'),
                            AFTER.replace('output y', 'input y'),
                            AFTER.replace('[1:0]', '[0:1]'),
                            AFTER.replace('(a, y)', '(y, a)'),
                            AFTER.replace('[1:0]', '[2:0]')):
            self.b.write_text(replacement)
            with self.assertRaises(ValueError): compare(self.a, self.b)

    def test_inverter_cannot_be_contracted(self):
        self.a.write_text(BEFORE.replace('sg13g2_buf_1', 'sg13g2_inv_1'))
        with self.assertRaises(ValueError): compare(self.a, self.b)

    def test_delay_cell_logic_is_transparent_but_timing_unqualified(self):
        for delay in (1, 2, 3):
            self.a.write_text(BEFORE.replace('sg13g2_buf_1', f'sg13g2_dlygate4sd{delay}_1'))
            result = compare(self.a, self.b)
            self.assertEqual(result['removed_buffers'], 2)
            self.assertFalse(result['timing_qualified'])

    def test_invalid_or_unsupported_syntax(self):
        for text in (BEFORE.replace('a[0]', 'a[9]'), BEFORE.replace('a[0]', 'unknown'),
                     BEFORE.replace('endmodule', 'assign y = ~a[0]; endmodule'),
                     BEFORE.replace('wire n;', 'wire [3:0] n;'),
                     BEFORE.replace('.X(m)', '.A(m)')):
            self.a.write_text(text)
            with self.assertRaises(ValueError): parse(self.a)

    def test_output_alias_is_preserved(self):
        for path, source in ((self.a, BEFORE), (self.b, AFTER)):
            path.write_text(source.replace('(a, y)', '(a, y, alias_y)')
                            .replace('output y;', 'output y; output alias_y;')
                            .replace('endmodule', 'assign alias_y = y; endmodule'))
        self.assertEqual(compare(self.a, self.b)['status'], 'pass')
        self.b.write_text(self.b.read_text().replace('alias_y = y', 'alias_y = a[1]'))
        with self.assertRaises(ValueError): compare(self.a, self.b)

    def test_escaped_bus(self):
        for path, text in ((self.a, BEFORE), (self.b, AFTER)):
            text = text.replace('(a,', '(\\bus.input ,').replace('[1:0] a;', '[1:0] \\bus.input ;')
            text = text.replace('a[', '\\bus.input [')
            path.write_text(text)
        self.assertEqual(compare(self.a, self.b)['status'], 'pass')

    def test_filler_deferral_requires_explicit_mode(self):
        self.a.write_text(AFTER.replace('endmodule', 'sg13g2_decap_8 FILLER_0_0 (); endmodule'))
        with self.assertRaises(ValueError): compare(self.a, self.b)
        result = compare(self.a, self.b, defer_fillers=True)
        self.assertEqual(result['removed_fillers'], 1)
        self.assertEqual(result['removed_buffers'], 0)
        self.assertFalse(result['physical_qualified'])

    def test_filler_deferral_cannot_change_logic(self):
        for cell in ('sg13g2_buf_1 extra (.A(a[0]), .X(y));',
                     'sg13g2_decap_8 extra (.A(a[0]));',
                     'unknown extra ();'):
            self.a.write_text(AFTER.replace('endmodule', cell + ' endmodule'))
            with self.assertRaises(ValueError): compare(self.a, self.b, defer_fillers=True)
        self.a.write_text(AFTER.replace('endmodule', 'sg13g2_fill_1 filler (); endmodule'))
        self.b.write_text(AFTER.replace('.A(a[0])', '.A(a[1])'))
        with self.assertRaises(ValueError): compare(self.a, self.b, defer_fillers=True)


if __name__ == '__main__':
    unittest.main()
