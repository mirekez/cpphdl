"""A completed DRC group cannot hide a changed design or another failed stage."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import complete_qualification as receipt


class Completion(unittest.TestCase):
    def test_default_technology_root_is_the_analog_package(self):
        self.assertTrue((receipt.ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py').is_file())

    def fixture(self, root):
        work = root / 'physical'
        work.mkdir()
        for name, text in dict(design='current design', **{'module_filled.gds': 'layout',
                'cpp_trace.csv': 'oracle', 'postroute_trace.csv': 'oracle', 'setup.rpt': 'setup',
                'hold.rpt': 'hold', 'electrical.rpt': 'electrical', 'lvs.lvsdb': 'LVS',
                'combined.lyrdb': 'DRC', 'drc.log': 'Completed running Antenna checks.'}).items():
            (work / name).write_text(text)
        (work / 'mapped.json').write_text(json.dumps({'modules': {'ReceiveFilter': {
            'ports': {'sample': {'bits': [1, 2]}}}}}))
        physical = work / 'physical_module.json'
        original = dict(status='failed', top='ReceiveFilter', errors=['drc: command timed out'],
            native_postroute_exact=True, stages=[dict(name=name, returncode=0)
            for name in ('fill', 'postroute-vectors', 'lvs')],
            input_sha256={str(work / 'design'): receipt.digest(work / 'design')},
            artifact_sha256={'module_filled.gds': receipt.digest(work / 'module_filled.gds')},
            lvs={'report': str(work / 'lvs.lvsdb')})
        physical.write_text(json.dumps(original))
        complete = root / 'completion.json'
        command = ['klayout', '-rd', f'input={work / "module_filled.gds"}', '-rd', 'topcell=ReceiveFilter']
        for key in ('precheck_drc', 'disable_extra_rules', 'no_feol', 'no_beol', 'no_offgrid',
                    'no_angle', 'no_forbidden', 'no_pin', 'no_recommended'):
            command += ['-rd', key + '=false']
        completed = dict(status='pass', command=command,
            input_sha256={str(path): receipt.digest(path) for path in (physical, work / 'module_filled.gds')},
            artifact_sha256={str(work / 'combined.lyrdb'): receipt.digest(work / 'combined.lyrdb')},
            drc={'report': str(work / 'combined.lyrdb')})
        complete.write_text(json.dumps(completed))
        return physical, complete

    def check(self, physical, complete):
        with patch.object(receipt, 'check_drc', return_value={'violations': 0}), \
             patch.object(receipt, 'check_lvs', return_value={'matched_circuits': 2}) as lvs, \
             patch.object(receipt, 'check_timing', return_value={'setup_slack_ns': .13, 'hold_slack_ns': .05}):
            result = receipt.verify(physical, complete, physical.parent / 'pdk')
            self.assertEqual(lvs.call_args.args[2], {'VDD', 'VSS', 'sample[0]', 'sample[1]'})
            return result

    def test_completed_same_layout_keeps_module_scope(self):
        with tempfile.TemporaryDirectory() as temporary:
            physical, complete = self.fixture(Path(temporary))
            result = self.check(physical, complete)
            self.assertEqual(result['status'], 'component_checks_pass')
            self.assertFalse(result['full_1000base_t_chip'])
            self.assertEqual(result['gds_sha256'], receipt.digest(physical.parent / 'module_filled.gds'))

    def test_other_failed_stage_is_never_overridden(self):
        with tempfile.TemporaryDirectory() as temporary:
            physical, complete = self.fixture(Path(temporary))
            data = json.loads(physical.read_text())
            data['errors'].append('timing: negative setup')
            physical.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'only DRC-stage'):
                self.check(physical, complete)

    def test_changed_design_or_routed_trajectory_is_rejected(self):
        for name in ('design', 'postroute_trace.csv'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                physical, complete = self.fixture(Path(temporary))
                (physical.parent / name).write_text('changed')
                with self.assertRaises(ValueError):
                    self.check(physical, complete)

    def test_other_layout_or_top_is_rejected(self):
        for fault in ('layout', 'top'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as temporary:
                physical, complete = self.fixture(Path(temporary))
                data = json.loads(complete.read_text())
                if fault == 'layout':
                    original = physical.parent / 'module_filled.gds'
                    other = physical.parent / 'another.gds'
                    other.write_bytes(original.read_bytes())
                    data['input_sha256'][str(other)] = data['input_sha256'].pop(str(original))
                else:
                    data['command'] = [part.replace('topcell=ReceiveFilter', 'topcell=Wrong') for part in data['command']]
                complete.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    self.check(physical, complete)

    def test_unfinished_antenna_check_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            physical, complete = self.fixture(Path(temporary))
            (physical.parent / 'drc.log').write_text('running antenna checks')
            with self.assertRaisesRegex(ValueError, 'antenna checking did not complete'):
                self.check(physical, complete)

    def test_conflicting_consumed_and_live_input_provenance_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            physical, complete = self.fixture(Path(temporary))
            design = physical.parent / 'design'
            retained = physical.parent / 'retained-design'
            retained.write_bytes(design.read_bytes())
            design.write_text('new live build artifact')
            original = json.loads(physical.read_text())
            original['retained_input_paths'] = {str(design): str(retained)}
            physical.write_text(json.dumps(original))
            completed = json.loads(complete.read_text())
            completed['input_sha256'][str(physical)] = receipt.digest(physical)
            completed['input_sha256'][str(design)] = receipt.digest(design)
            complete.write_text(json.dumps(completed))
            with self.assertRaisesRegex(ValueError, 'conflicting qualification provenance'):
                self.check(physical, complete)

    def test_disabled_rules_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            physical, complete = self.fixture(Path(temporary))
            data = json.loads(complete.read_text())
            data['command'].remove('no_angle=false')
            complete.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'disabled required checks'):
                self.check(physical, complete)


if __name__ == '__main__':
    unittest.main()
