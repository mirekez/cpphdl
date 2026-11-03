"""Do not spend a detailed route on a candidate failing the optimization screen."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_margin_route import margin_screen, nonnegative_setup_target


class MarginScreen(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        for prefix in ('global', 'margin'):
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                (self.work / (prefix + '_' + kind + '.rpt')).write_text('worst slack ' + mode + ' 0.15')
        (self.work / 'global_electrical.rpt').write_text('')

    def test_both_estimates_pass(self):
        result = margin_screen(self.work)
        self.assertEqual(result['optimization']['setup'], .15)
        self.assertEqual(result['nominal']['hold'], .15)

    def test_optimistic_nominal_slack_cannot_hide_failed_wire_margin(self):
        (self.work / 'margin_setup.rpt').write_text('worst slack max -0.427')
        with self.assertRaisesRegex(ValueError, '0.1 ns margin'):
            margin_screen(self.work)

    def test_nominal_hold_must_still_pass(self):
        (self.work / 'global_hold.rpt').write_text('worst slack min -0.001')
        with self.assertRaisesRegex(ValueError, '0.1 ns margin'):
            margin_screen(self.work)

    def test_electrical_failure_rejects_route(self):
        (self.work / 'global_electrical.rpt').write_text('max slew VIOLATED')
        with self.assertRaisesRegex(ValueError, 'electrical limits'):
            margin_screen(self.work)

    def test_nonfinite_slack_rejected(self):
        (self.work / 'margin_setup.rpt').write_text('worst slack max nan')
        with self.assertRaisesRegex(ValueError, 'finite slack'):
            margin_screen(self.work)

    def test_explicit_nonnegative_screen_does_not_accept_negative_setup(self):
        (self.work / 'margin_setup.rpt').write_text('worst slack max 0.014')
        with self.assertRaises(ValueError):
            margin_screen(self.work)
        self.assertEqual(margin_screen(self.work, 0)['optimization']['setup'], .014)
        (self.work / 'margin_setup.rpt').write_text('worst slack max -0.001')
        with self.assertRaises(ValueError):
            margin_screen(self.work, 0)
        (self.work / 'margin_setup.rpt').write_text('worst slack max 0.014')
        (self.work / 'global_hold.rpt').write_text('worst slack min 0.099')
        with self.assertRaises(ValueError):
            margin_screen(self.work, 0)

    def test_setup_target_cannot_be_negative_or_nonfinite(self):
        for value in (-.1, float('inf'), float('nan'), True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                nonnegative_setup_target(value)



if __name__ == '__main__':
    unittest.main()
