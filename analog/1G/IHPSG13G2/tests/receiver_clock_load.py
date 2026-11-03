"""Keep clock-root extraction complete and expose excessive integration loads."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_clock_load import budget, digest, input_spef, run
from receive_interface_loads import extract
from receive_interface_loads_test import SPEF, LIBRARY


class ClockLoad(unittest.TestCase):
    def test_streamed_selection_keeps_complete_clock_net(self):
        source = SPEF.replace('comparisons_in[0]', 'clk')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'core.spef'
            path.write_text(source + '*D_NET *3 0\n*CONN\n*CAP\n*RES\n*END\n')
            selected = input_spef(path)
            self.assertEqual(selected, source)
            record = extract(selected, {'slow': LIBRARY}, ports=['clk'])[0]
            self.assertAlmostEqual(budget(record, 100)['remaining_external_wire_ff'], 76)
            self.assertFalse(budget(record, 20)['fits_before_external_wiring'])
            self.assertAlmostEqual(budget(record, 20)['remaining_external_wire_ff'], -4)

    def test_missing_and_ambiguous_clock_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'core.spef'
            for text in (SPEF, SPEF.replace('*1 comparisons_in[0]', '*1 clk\n*4 clk')):
                path.write_text(text)
                with self.assertRaisesRegex(ValueError, 'ambiguous receiver input'):
                    input_spef(path)

    def test_reset_selection_and_streamed_hash(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'core.spef'
            source = SPEF.replace('comparisons_in[0]', 'reset')
            path.write_text(source)
            record = extract(input_spef(path, 'reset'), {'slow': LIBRARY}, ports=['reset'])[0]
            self.assertAlmostEqual(budget(record, 80)['remaining_external_wire_ff'], 56)
            # Exercise multiple read chunks and compare against an independent
            # one-shot hash, ensuring bounded hashing does not change receipts.
            path.write_bytes(b'abcd' * 600000)
            self.assertEqual(digest(path), hashlib.sha256(path.read_bytes()).hexdigest())

    def test_invalid_limits(self):
        for limit in (0, -1, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                budget({}, limit)

    def test_combined_receipt_and_stale_extraction_rejection(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            route, pdk = root / 'route', root / 'pdk'
            route.mkdir()
            clock = SPEF.replace('comparisons_in[0]', 'clk')
            reset = SPEF.replace('comparisons_in[0]', 'reset')
            for a, b in (('*1', '*4'), ('*2', '*5'), ('*3', '*6')):
                reset = reset.replace(a, b)
            clock = clock.replace('*3 other_net\n', '*3 other_net\n*4 reset\n*5 delay_2\n*6 other_reset_net\n')
            spef = route / 'core.spef'
            spef.write_text('*DESIGN "PCSReceiver"\n' + clock + '*D_NET' + reset.split('*D_NET', 1)[1])
            hashes = {}
            for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
                library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib' / f'sg13g2_stdcell_{corner}.lib'
                library.parent.mkdir(parents=True, exist_ok=True)
                library.write_text(LIBRARY)
                hashes[str(library)] = digest(library)
            proof = dict(status='early_route_diagnostic_complete', stage='complete',
                         connectivity=dict(status='pass', top='PCSReceiver'),
                         source_sha256=hashes, artifact_sha256={str(spef): digest(spef)})
            (route / 'checks.json').write_text(json.dumps(proof))
            args = SimpleNamespace(route=route, pdk=pdk, output=root / 'loads.json', budget_ff=55, reset_budget_ff=80)
            run(args)
            report = json.loads(args.output.read_text())
            self.assertAlmostEqual(report['budget']['remaining_external_wire_ff'], 31)
            self.assertAlmostEqual(report['reset_budget']['remaining_external_wire_ff'], 56)
            self.assertFalse(report['clock_interface_qualified'])
            self.assertFalse(report['reset_interface_qualified'])
            self.assertFalse(report['full_chip_qualified'])
            spef.write_text(spef.read_text().replace('0.010', '0.011'))
            args.output = root / 'stale.json'
            with self.assertRaisesRegex(ValueError, 'changed receiver SPEF'):
                run(args)
            self.assertFalse(args.output.exists())

    def test_no_extracted_clock_section_cannot_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'core.spef'
            path.write_text(SPEF.replace('comparisons_in[0]', 'clk').split('*D_NET')[0])
            with self.assertRaisesRegex(ValueError, 'missing requested input'):
                extract(input_spef(path), {'slow': LIBRARY}, ports=['clk'])


if __name__ == '__main__':
    unittest.main()
