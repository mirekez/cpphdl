"""Buffer replacement must preserve logic, direction, and every surviving pin."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rebuffer_equivalence import compare


class RebufferEquivalence(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        work = Path(temporary.name)
        self.before, self.after, self.models = [work / n for n in ('before.v', 'after.v', 'cells.v')]
        self.before.write_text('''module PCSReceiver(a, b, y);
input a; input b; output y; wire n; wire m;
sg13g2_nand2_1 gate(.A(a), .B(b), .Y(n));
sg13g2_buf_1 old1(.A(n), .X(m));
sg13g2_buf_1 old2(.A(m), .X(y)); endmodule
''')
        self.text = '''module PCSReceiver(a, b, y);
input a; input b; output y; wire n;
sg13g2_nand2_2 gate(.A(a), .B(b), .Y(n));
sg13g2_buf_2 replacement(.A(n), .X(y)); endmodule
'''
        self.after.write_text(self.text)
        self.models.write_text(''.join(f'module sg13g2_buf_{s}(A, X); input A; output X; assign X=A; endmodule\n'
            for s in (1, 2)) + ''.join(f'module sg13g2_nand2_{s}(A, B, Y); input A,B; output Y; assign Y=~(A&B); endmodule\n'
            for s in (1, 2)))

    def test_replacement_and_sizing(self):
        result = compare(self.before, self.after, self.models)
        self.assertEqual(result['removed_buffers'], ['old1', 'old2'])
        self.assertEqual(result['inserted_buffers'], ['replacement'])
        self.assertEqual(len(result['replacements']), 1)
        self.assertFalse(result['timing_qualified'])

    def test_reversed_buffer_is_not_an_identity_wire(self):
        self.after.write_text(self.text.replace('replacement(.A(n), .X(y))', 'replacement(.A(y), .X(n))'))
        with self.assertRaisesRegex(ValueError, 'another driver'):
            compare(self.before, self.after, self.models)

    def test_wrong_branch(self):
        self.after.write_text(self.text.replace('replacement(.A(n)', 'replacement(.A(a)'))
        with self.assertRaisesRegex(ValueError, 'endpoint connectivity'):
            compare(self.before, self.after, self.models)

    def test_removed_logic_cell(self):
        self.after.write_text(self.text.replace('sg13g2_nand2_2 gate(.A(a), .B(b), .Y(n));', ''))
        with self.assertRaisesRegex(ValueError, 'only proven identity'):
            compare(self.before, self.after, self.models)

    def test_changed_variant_function(self):
        self.models.write_text(self.models.read_text().replace(
            'sg13g2_nand2_2(A, B, Y); input A,B; output Y; assign Y=~(A&B);',
            'sg13g2_nand2_2(A, B, Y); input A,B; output Y; assign Y=(A&B);'))
        with self.assertRaisesRegex(ValueError, 'logic function'):
            compare(self.before, self.after, self.models)

    def test_inverting_buffer_model(self):
        self.models.write_text(self.models.read_text().replace('assign X=A;', 'assign X=~A;'))
        with self.assertRaisesRegex(ValueError, 'only proven identity'):
            compare(self.before, self.after, self.models)

    def test_undriven_buffer_input(self):
        self.after.write_text(self.text.replace('wire n;', 'wire n; wire floating;').replace('replacement(.A(n)', 'replacement(.A(floating)'))
        with self.assertRaisesRegex(ValueError, 'unique driver'):
            compare(self.before, self.after, self.models)

    def test_disconnected_buffer_cycle_is_rejected(self):
        self.after.write_text(self.text.replace('wire n;', 'wire n; wire c1; wire c2;').replace(
            'endmodule', 'sg13g2_buf_1 cycle1(.A(c1), .X(c2));\n'
            'sg13g2_buf_1 cycle2(.A(c2), .X(c1)); endmodule'))
        with self.assertRaisesRegex(ValueError, 'directed cycle'):
            compare(self.before, self.after, self.models)

    def test_buffer_cannot_be_replaced_by_an_inverter(self):
        self.after.write_text(self.text.replace('sg13g2_buf_2 replacement', 'sg13g2_inv_2 replacement'))
        with self.assertRaisesRegex(ValueError, 'only proven identity'):
            compare(self.before, self.after, self.models)

    def test_unconnected_load_cell_output_is_preserved(self):
        self.models.write_text(self.models.read_text() +
            'module sg13g2_inv_1(A, Y); input A; output Y; assign Y=~A; endmodule\n')
        for p in (self.before, self.after):
            p.write_text(p.read_text().replace('endmodule', 'sg13g2_inv_1 clkload0(.A(a)); endmodule'))
        self.assertEqual(compare(self.before, self.after, self.models)['status'], 'pass')
        self.after.write_text(self.after.read_text().replace('clkload0(.A(a))', 'clkload0(.A(b))'))
        with self.assertRaisesRegex(ValueError, 'endpoint connectivity'):
            compare(self.before, self.after, self.models)


if __name__ == '__main__':
    unittest.main()
