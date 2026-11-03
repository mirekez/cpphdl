"""Final sizing must not hide enlarged-wire electrical or timing failures."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_final_repair import final_screen, repair_margin, require_complete_routes


class FinalRepair(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        (self.work / 'global.log').write_text('RECEIVER_FINAL_ROUTE_CHECK_BEGIN\nRECEIVER_FINAL_ROUTE_CHECK_END\n')
        for prefix in ('global', 'margin'):
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                (self.work / f'{prefix}_{kind}.rpt').write_text(f'worst slack {mode} 0.15\n')
            (self.work / f'{prefix}_electrical.rpt').write_text('')

    def test_all_final_estimates_pass(self):
        self.assertEqual(final_screen(self.work)['optimization']['setup'], .15)

    def test_pin_loading_failure_cannot_hide_behind_nominal_pass(self):
        (self.work / 'margin_electrical.rpt').write_text('pin X 0.10 0.12 -0.02 (VIOLATED)')
        with self.assertRaisesRegex(ValueError, 'enlarged-C electrical'):
            final_screen(self.work)

    def test_missing_enlarged_electrical_report_is_rejected(self):
        (self.work / 'margin_electrical.rpt').unlink()
        with self.assertRaises(FileNotFoundError):
            final_screen(self.work)

    def test_each_final_setup_and_hold_screen_is_required(self):
        for prefix in ('global', 'margin'):
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                with self.subTest(prefix=prefix, kind=kind):
                    p = self.work / f'{prefix}_{kind}.rpt'
                    p.write_text(f'worst slack {mode} 0.099\n')
                    with self.assertRaises(ValueError):
                        final_screen(self.work)
                    p.write_text(f'worst slack {mode} 0.15\n')

    def test_original_electrical_limits_are_still_required(self):
        (self.work / 'global_electrical.rpt').write_text('max capacitance VIOLATED')
        with self.assertRaisesRegex(ValueError, 'nominal global electrical'):
            final_screen(self.work)

    def test_extra_margin_can_be_zero_without_changing_screen(self):
        self.assertEqual(repair_margin(0), 0)
        self.assertEqual(repair_margin('30'), 30)
        (self.work / 'margin_electrical.rpt').write_text('max capacitance VIOLATED')
        with self.assertRaises(ValueError):
            final_screen(self.work)

    def test_invalid_extra_margins_are_rejected(self):
        for value in (-1, 41, float('nan'), float('inf'), True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                repair_margin(value)

    def test_incomplete_route_cannot_supply_a_timing_estimate(self):
        for warning in ('[WARNING EST-0026] Missing route to pin place61510/X in net net61498.',
                        'Missing route to pin input/A'):
            with self.subTest(warning=warning), self.assertRaisesRegex(ValueError, 'unrouted pin'):
                require_complete_routes('RECEIVER_FINAL_ROUTE_CHECK_BEGIN\n' + warning + '\nRECEIVER_FINAL_ROUTE_CHECK_END')

    def test_optimizer_warning_requires_clean_final_reroute(self):
        require_complete_routes('[WARNING EST-0026] Missing route to pin temporary/X\n'
            'RECEIVER_FINAL_ROUTE_CHECK_BEGIN\nRECEIVER_FINAL_ROUTE_CHECK_END\n')

    def test_missing_or_repeated_final_route_check_rejected(self):
        for log in ('', 'RECEIVER_FINAL_ROUTE_CHECK_BEGIN',
                    'RECEIVER_FINAL_ROUTE_CHECK_END\nRECEIVER_FINAL_ROUTE_CHECK_BEGIN',
                    'RECEIVER_FINAL_ROUTE_CHECK_BEGIN\nRECEIVER_FINAL_ROUTE_CHECK_BEGIN\nRECEIVER_FINAL_ROUTE_CHECK_END'):
            with self.subTest(log=log), self.assertRaisesRegex(ValueError, 'complete final route'):
                require_complete_routes(log)


if __name__ == '__main__':
    unittest.main()
