#!/usr/bin/env python3
import unittest
from comparison_revision import verify_json_roundtrip_revision


class RevisionTest(unittest.TestCase):
    original = '''LIMIT = .010
def run(args):
    return args.circuit
def compare(args):
    if any(result.get(key) != value for key, value in current.items()):
        raise ValueError('mismatch')
'''

    def corrected(self):
        return self.original.replace('result.get(key) != value', 'result.get(key) != json.loads(json.dumps(value))')

    def test_only_the_json_representation_fix_is_allowed(self):
        verify_json_roundtrip_revision(self.original, self.corrected())

    def test_physics_or_tolerance_or_other_checker_changes_rejected(self):
        for text in [self.corrected().replace('args.circuit', 'args.other_circuit'),
                     self.corrected().replace('.010', '.020'),
                     self.corrected().replace("raise ValueError('mismatch')", 'pass'),
                     self.corrected().replace('def compare(args):', 'def compare(args, extra=True):')]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                verify_json_roundtrip_revision(self.original, text)


if __name__ == '__main__':
    unittest.main()
