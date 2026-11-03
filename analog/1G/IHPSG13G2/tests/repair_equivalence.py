"""Reject logic/topology defects in combined buffer insertion and cell sizing."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from repair_equivalence import compare


class RepairEquivalence(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.before, self.after, self.models = [self.root / name for name in ('before.v', 'after.v', 'cells.v')]
        self.before.write_text('''module PCSReceiver(a, b, y);
input a; input b; output y; wire n;
sg13g2_nand2_1 gate(.A(a), .B(b), .Y(n));
sg13g2_buf_1 out(.A(n), .X(y)); endmodule
''')
        self.text = '''module PCSReceiver(a, b, y);
input a; input b; output y; wire n; wire m;
sg13g2_nand2_2 gate(.A(a), .B(b), .Y(n));
sg13g2_buf_2 repair(.A(n), .X(m));
sg13g2_buf_1 out(.A(m), .X(y)); endmodule
'''
        self.after.write_text(self.text)
        self.models.write_text(''.join('module sg13g2_buf_' + str(s) +
            '(A, X); input A; output X; assign X=A; endmodule\n' for s in (1, 2)) +
            ''.join('module sg13g2_nand2_' + str(s) +
            '(A, B, Y); input A,B; output Y; assign Y=~(A&B); endmodule\n' for s in (1, 2)))

    def test_buffer_insertion_and_same_function_sizing(self):
        result = compare(self.before, self.after, self.models)
        self.assertEqual(result['inserted_buffers'], ['repair'])
        self.assertEqual(len(result['replacements']), 1)
        self.assertFalse(result['physical_qualified'])

    def test_wrong_branch_rejected(self):
        self.after.write_text(self.text.replace('repair(.A(n)', 'repair(.A(a)'))
        with self.assertRaisesRegex(ValueError, 'endpoint connectivity'):
            compare(self.before, self.after, self.models)

    def test_non_buffer_insertion_rejected(self):
        self.after.write_text(self.text.replace('sg13g2_buf_2 repair', 'sg13g2_inv_2 repair'))
        with self.assertRaisesRegex(ValueError, 'non-inverting buffer'):
            compare(self.before, self.after, self.models)

    def test_changed_size_variant_function_rejected(self):
        self.models.write_text(self.models.read_text().replace(
            'module sg13g2_nand2_2(A, B, Y); input A,B; output Y; assign Y=~(A&B);',
            'module sg13g2_nand2_2(A, B, Y); input A,B; output Y; assign Y=(A&B);'))
        with self.assertRaisesRegex(ValueError, 'logic function'):
            compare(self.before, self.after, self.models)

    def test_removed_existing_cell_rejected(self):
        self.after.write_text(self.text.replace('sg13g2_buf_1 out(.A(m), .X(y));', ''))
        with self.assertRaisesRegex(ValueError, 'removed existing'):
            compare(self.before, self.after, self.models)

    def test_corrupt_common_buffer_model_rejected(self):
        self.models.write_text(self.models.read_text().replace('assign X=A;', 'assign X=~A;'))
        with self.assertRaisesRegex(ValueError, 'explicitly implement X = A'):
            compare(self.before, self.after, self.models)

    def test_characterized_identity_delay_cell_can_change_but_does_not_qualify_hold(self):
        self.models.write_text(self.models.read_text() +
            'module sg13g2_dlygate4sd2_1(A, X); input A; output X; assign X=A; endmodule\n')
        self.before.write_text(self.before.read_text().replace('sg13g2_buf_1 out', 'sg13g2_dlygate4sd2_1 out'))
        result = compare(self.before, self.after, self.models)
        self.assertEqual(len(result['replacements']), 2)
        self.assertFalse(result['timing_qualified'])


if __name__ == '__main__':
    unittest.main()
