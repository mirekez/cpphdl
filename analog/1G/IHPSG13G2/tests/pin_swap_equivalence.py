"""Input permutations must preserve every output for every input combination."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pin_swap_equivalence import compare, combinational_model, truth_table


class PinSwapEquivalence(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.before, self.after, self.library = [root / n for n in ('before.v', 'after.v', 'cells.v')]
        self.original = '''module PCSReceiver(a, b, c, y);
input a; input b; input c; output y;
sg13g2_a21oi_1 gate(.A1(a), .A2(b), .B1(c), .Y(y)); endmodule
'''
        self.before.write_text(self.original)
        self.after.write_text(self.original.replace('.A1(a), .A2(b)', '.A1(b), .A2(a)'))
        # Assignments deliberately precede the definitions of their dependencies,
        # as in the retained Yosys Liberty-derived IHP models.
        self.library.write_text('''module sg13g2_a21oi_1(Y, A1, A2, B1);
input A1; input A2; input B1; output Y; wire n; wire m;
assign Y = ~n; assign n = m | B1; assign m = A1 & A2; endmodule
''')

    def check(self):
        return compare(self.before, self.after, self.library)

    def test_symmetric_inputs(self):
        proof = self.check()
        self.assertEqual(proof['changed_cells'][0]['pins'], ['A1', 'A2'])
        self.assertFalse(proof['physical_qualified'])

    def test_same_input_multiset_is_insufficient(self):
        self.after.write_text(self.original.replace('.A1(a)', '.A1(c)').replace('.B1(c)', '.B1(a)'))
        with self.assertRaisesRegex(ValueError, 'Boolean function'):
            self.check()

    def test_missing_input_is_rejected(self):
        self.after.write_text(self.original.replace('.A1(a)', '.A1(b)'))
        with self.assertRaisesRegex(ValueError, 'input-net multiset'):
            self.check()

    def test_output_rewiring_is_rejected(self):
        self.after.write_text(self.original.replace('.Y(y)', '.Y(a)'))
        with self.assertRaisesRegex(ValueError, 'output connection'):
            self.check()

    def test_master_or_instance_change_is_rejected(self):
        for text in (self.original.replace('a21oi_1', 'a21oi_2'),
                     self.original.replace(' gate(', ' other(')):
            self.after.write_text(text)
            with self.assertRaises(ValueError):
                self.check()

    def test_sequential_rewiring_is_rejected(self):
        self.library.write_text('''module sg13g2_a21oi_1(Y,A1,A2,B1);
input A1; input A2; input B1; output Y; reg Y;
always @(posedge B1) Y <= A1 & A2; endmodule
''')
        with self.assertRaisesRegex(ValueError, 'combinational model'):
            self.check()

    def test_every_output_must_agree(self):
        for path in (self.before, self.after):
            path.write_text(path.read_text().replace('c, y)', 'c, y, z)').replace(
                'output y;', 'output y; output z;').replace('.Y(y)', '.Y(y), .Z(z)'))
        self.library.write_text(self.library.read_text().replace('(Y, A1', '(Y, Z, A1').replace(
            'output Y;', 'output Y; output Z; assign Z = A1;'))
        with self.assertRaisesRegex(ValueError, 'Boolean function'):
            self.check()

    def test_parser_rejects_cycles_undriven_names_and_calls(self):
        for expression in ('n', 'unknown', 'f(A1)'):
            text = self.library.read_text().replace('assign n = m | B1;', 'assign n = ' + expression + ';')
            original = self.library.read_text()
            self.library.write_text(text)
            with self.subTest(expression=expression), self.assertRaises(ValueError):
                self.check()
            self.library.write_text(original)

    def test_tied_inputs_preserve_multiplicity(self):
        self.before.write_text(self.original.replace('.A2(b)', '.A2(a)'))
        self.after.write_text(self.before.read_text().replace('.A1(a)', '.A1(c)').replace('.B1(c)', '.B1(a)'))
        with self.assertRaisesRegex(ValueError, 'Boolean function'):
            self.check()

    def test_truth_table_matches_independent_boolean_reference(self):
        model = combinational_model('(Y,A,B,C);inputA,B,C;outputY;wireq;assignY=~q;assignq=(A&B)|C;')
        table = truth_table(model, {'A': 'a', 'B': 'b', 'C': 'c'}, ['a', 'b', 'c'])['Y']
        for row in range(8):
            expected = not (((row & 1) and (row & 2)) or (row & 4))
            self.assertEqual((table >> row) & 1, int(expected))


if __name__ == '__main__':
    unittest.main()
