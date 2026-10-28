#!/usr/bin/env python3
"""No stale or mismatched gate evidence may start module implementation."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('physical_receive_module', SOURCE / 'physical_module.py')
flow = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flow)
spec = importlib.util.spec_from_file_location('routed_timing_recheck', SOURCE / 'recheck_timing.py')
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


class PhysicalModule(unittest.TestCase):
    def test_resized_receiver_checkpoint_keeps_identity_and_timing_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            origin, work = root / 'origin', root / 'work'
            origin.mkdir()
            work.mkdir()
            source = root / 'design.hpp'
            source.write_text('same receiver')
            for name in ('mapped.v', 'resized_global.odb', 'resized_global.sdc'):
                (origin / name).write_text('saved ' + name)
            (work / 'mapped.v').write_bytes((origin / 'mapped.v').read_bytes())
            constraints = dict(period_ns=1000 / 136)
            evidence = dict(top='PCSReceiver', constraints=constraints,
                input_sha256={str(source): flow.digest(source)},
                resized_checkpoint_sha256={name: flow.digest(origin / name)
                    for name in ('resized_global.odb', 'resized_global.sdc')})
            (origin / 'physical_module.json').write_text(json.dumps(evidence))
            result = flow.import_placement(origin, work, 'PCSReceiver', constraints, 'resized_global')
            self.assertEqual(result['checkpoint_stage'], 'resized_global')
            self.assertEqual((work / 'resized_global.odb').read_bytes(), (origin / 'resized_global.odb').read_bytes())
            with self.assertRaisesRegex(ValueError, 'timing contract'):
                flow.import_placement(origin, work, 'PCSReceiver', dict(period_ns=8), 'resized_global')
            with self.assertRaisesRegex(ValueError, 'unsupported'):
                flow.import_placement(origin, work, 'ReceiveFilter', constraints, 'resized_global')
            (origin / 'resized_global.odb').write_text('different sizing')
            with self.assertRaisesRegex(ValueError, 'artifact changed'):
                flow.import_placement(origin, work, 'PCSReceiver', constraints, 'resized_global')

    def test_def_import_preserves_public_array_bit_identity(self):
        mapped = 'output [0:2] acquire_out; output [0:2] convert_out; output [23:0] trial_out;'
        routed = 'output [2:0] acquire_out; output [2:0] convert_out; output [23:0] trial_out;'
        interface = ('module Top(output wire acquire_out[3], convert_out[3],\n'
            'output wire [7:0] trial_out[3]);\n'
            'Top_mapped impl(.acquire_out({acquire_out[0], acquire_out[1], acquire_out[2]}),'
            '.convert_out({convert_out[0], convert_out[1], convert_out[2]}),'
            '.trial_out({trial_out[0], trial_out[1], trial_out[2]})); endmodule')
        adapted, changes = flow.routed_array_interface(mapped, routed, interface)
        self.assertIn('.acquire_out({acquire_out[2], acquire_out[1], acquire_out[0]})', adapted)
        self.assertIn('.convert_out({convert_out[2], convert_out[1], convert_out[0]})', adapted)
        self.assertIn('.trial_out({trial_out[0], trial_out[1], trial_out[2]})', adapted)
        self.assertEqual(set(changes), {'acquire_out', 'convert_out'})
        self.assertEqual(flow.routed_array_interface(mapped, mapped, interface), (interface, {}))
        with self.assertRaisesRegex(ValueError, 'index set changed'):
            flow.routed_array_interface(mapped, routed.replace('[2:0] acquire_out', '[3:1] acquire_out'), interface)
        with self.assertRaisesRegex(ValueError, 'unsupported routed array permutation'):
            flow.routed_array_interface(mapped, routed.replace('[23:0] trial_out', '[0:23] trial_out'), interface)
        with self.assertRaisesRegex(ValueError, 'missing or ambiguous'):
            flow.routed_array_interface(mapped, routed.replace('output [2:0] convert_out;', ''), interface)

    def test_timing_recheck_keeps_adc_clock_and_all_analog_loads(self):
        original = dict(period_ns=4, clock_uncertainty_ns=.1, input_output_delay_ns=.4,
                        accepted_transition_ns=.5, accepted_fanout=8,
                        accepted_capacitance_pf=.25, analog_control_load_pf=.16)
        actual, script = timing.timing_contract(dict(top='ADCReceiveController', constraints=original))
        self.assertEqual(actual, original)
        self.assertIn('-period 4 [get_ports clk]', script)
        self.assertIn('llength $analog_controls] != 160', script)
        self.assertIn('set_load 0.16 $analog_controls', script)
        self.assertIn('set_max_capacitance 0.25', script)
        for top, count in (('InterleavedSARBankController', 130), ('ADCReceiveLaneController', 162)):
            with self.subTest(top=top):
                _, array_script = timing.timing_contract(dict(top=top, constraints=original))
                self.assertIn(f'llength $analog_controls] != {count}', array_script)
                self.assertIn('set_load 0.16 $analog_controls', array_script)
                if top == 'ADCReceiveLaneController':
                    self.assertIn('clear_out evaluate_enable_out', array_script)
        with self.assertRaisesRegex(ValueError, 'unknown module'):
            timing.timing_contract(dict(top='Unknown', constraints=original))
        original['period_ns'] = 3.8
        self.assertIn('-period 3.8', timing.timing_contract(dict(top='ADCReceiveController', constraints=original))[1])
        for key in tuple(original):
            broken = dict(original)
            broken.pop(key)
            with self.subTest(missing=key), self.assertRaisesRegex(ValueError, 'constraints'):
                timing.timing_contract(dict(top='ADCReceiveController', constraints=broken))

    def test_bounded_execution_cannot_bypass_large_module_memory_lock(self):
        with self.assertRaisesRegex(ValueError, 'limited to the small clock controller'):
            flow.implement(SimpleNamespace(module='PCSReceiver', bounded_clock_run=True))

    def test_processing_clock_recheck_preserves_both_domains_and_scope(self):
        original = dict(period_ns=1.25, reference_period_ns=8,
                        cross_domain_timing='excluded', clock_uncertainty_ns=.1,
                        input_output_delay_ns=.4, accepted_transition_ns=.5,
                        accepted_fanout=8, accepted_capacitance_pf=.05,
                        analog_control_load_pf=.005)
        actual, script = timing.timing_contract(dict(top='ProcessingClockControl', constraints=original))
        self.assertEqual(actual, original)
        self.assertIn('-period 8 [get_ports ref_clk]', script)
        self.assertIn('-period 1.25 [get_ports pcs_clk]', script)
        self.assertIn('-clock acquisition [get_ports ready_pcs_out]', script)
        self.assertNotIn('[get_ports clk]', script)
        self.assertIn('set_clock_groups -asynchronous', script)
        self.assertIn('set_load 0.005 [get_ports ready_pcs_out]', script)
        loaded = dict(original, ready_output_load_pf=.08)
        actual, loaded_script = timing.timing_contract(dict(top='ProcessingClockControl', constraints=loaded))
        self.assertEqual(actual, loaded)
        self.assertIn('set_load 0.08 [get_ports ready_pcs_out]', loaded_script)
        for key in ('reference_period_ns', 'cross_domain_timing'):
            broken = dict(original)
            broken.pop(key)
            with self.assertRaisesRegex(ValueError, 'clock-domain constraints'):
                timing.timing_contract(dict(top='ProcessingClockControl', constraints=broken))
        for bad in (0, float('nan'), float('inf'), True):
            with self.assertRaisesRegex(ValueError, 'invalid processing-clock'):
                flow.processing_clock_constraints(bad)

    def test_reset_recheck_preserves_three_domains_and_output_loading(self):
        original = dict(period_ns=4, reference_period_ns=8, pcs_period_ns=1000 / 136,
                        cross_domain_timing='excluded', clock_uncertainty_ns=.1,
                        input_output_delay_ns=.4, output_load_pf=.08,
                        accepted_transition_ns=.3, accepted_fanout=8,
                        accepted_capacitance_pf=.1, analog_control_load_pf=.005)
        actual, script = timing.timing_contract(dict(top='PHYResetControl', constraints=original))
        self.assertEqual(actual, original)
        self.assertEqual(script.count('create_clock '), 3)
        for clock, pin in (('reference', 'ref'), ('adc', 'adc'), ('processing', 'pcs')):
            self.assertIn(f'-clock {clock} [get_ports reset_{pin}_out]', script)
        self.assertIn('set_load 0.08 [all_outputs]', script)
        self.assertNotIn('[get_ports clk]', script)
        for key in ('reference_period_ns', 'pcs_period_ns', 'output_load_pf', 'cross_domain_timing'):
            broken = dict(original)
            broken.pop(key)
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'reset clock-domain constraints'):
                timing.timing_contract(dict(top='PHYResetControl', constraints=broken))
        for name in ('reference_period_ns', 'adc_period_ns', 'pcs_period_ns',
                     'uncertainty_ns', 'io_delay_ns', 'output_load_pf'):
            for bad in (0, -1, float('nan'), float('inf'), True):
                with self.subTest(name=name, bad=bad), self.assertRaisesRegex(ValueError, 'invalid PHY reset'):
                    flow.phy_reset_constraints(**{name: bad})
        with self.assertRaisesRegex(ValueError, 'half-cycle'):
            flow.phy_reset_constraints(adc_period_ns=.8)

    def test_failed_extracted_timing_retains_route_and_stops_before_layout_checks(self):
        self.check_failed_route(False)

    def test_routed_database_still_requires_equivalence_and_new_timing(self):
        self.check_failed_route(True)

    def test_large_module_separates_global_detail_and_extraction(self):
        self.check_failed_route(False, large=True)

    def check_failed_route(self, resume_routed, large=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            mapped = root / 'mapped'
            mapped.mkdir()
            source = root / 'design.hpp'
            source.write_text('qualified source')
            for name in ('mapped.v', 'cells.v'):
                (mapped / name).write_text('qualified ' + name)
            (mapped / 'cpp_trace.csv').write_text('independent trace\n')
            (mapped / 'mapped.json').write_text(json.dumps({'modules': {'PAM5DriverControl': {'ports': {}}}}))
            (mapped / 'hardware_checks.json').write_text(json.dumps(dict(status='pass',
                native_mapped_exact=True, module='PAM5DriverControl',
                mapped_sha256=flow.digest(mapped / 'mapped.v'), source_sha256={str(source): flow.digest(source)})))
            for name in ('pdk/.cpphdl/venv/bin/python', 'tools/bin/openroad', 'tools/root/usr/bin/openroad',
                         'tools/bin/klayout', 'tools/root/usr/bin/klayout', 'tools/rcx_patterns.rules', 'verilator', 'cxx'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            args = SimpleNamespace(module='PAM5DriverControl', work=root / 'run', mapped_work=mapped,
                                   pdk=root / 'pdk', tools=root / 'tools',
                                   verilator=str(root / 'verilator'), cxx=str(root / 'cxx'))

            if resume_routed:
                args.resume_routed_database = root / 'checkpoint.odb'
                args.resume_routed_database.write_text('routed database')

            def execute(command, **options):
                work, environment = Path(options['cwd']), options['env']
                if Path(command[0]).name == 'openroad':
                    if environment.get('REPEATER_STAGE') == 'place':
                        names = ('placed.odb', 'placed.sdc')
                    elif Path(command[-1]).name == 'route_extract.tcl' and environment.get('REPEATER_STAGE') == 'global_route':
                        names = ('global_repaired.odb', 'global_repaired.sdc')
                    elif Path(command[-1]).name == 'detail_route.tcl':
                        names = ('core.odb', 'core.def')
                    else:
                        names = ('postroute.v', 'core.cdl', 'core.spef', 'core.def')
                    for name in names:
                        (work / name).write_text('routed ' + name)
                if Path(command[0]).name == 'VPAM5DriverControl':
                    (work / 'postroute_trace.csv').write_text('independent trace\n')
                return SimpleNamespace(returncode=0)

            with patch.object(flow, 'execute', side_effect=execute), \
                    patch.object(flow, 'check_timing', side_effect=ValueError('setup timing failed: -0.09 ns')), \
                    patch.dict(flow.MODULES['PAM5DriverControl'], {'large': large}), \
                    patch.object(flow, 'stream') as stream:
                with self.assertRaisesRegex(ValueError, 'setup timing failed'):
                    flow.implement(args)
                stream.assert_not_called()
            report = json.loads((args.work / 'physical_module.json').read_text())
            self.assertEqual(report['status'], 'failed')
            self.assertTrue(report['native_postroute_exact'])
            expected_artifacts = {'postroute.v', 'core.cdl', 'core.spef', 'core.def'}
            if large:
                self.assertEqual((args.work / 'core.odb').read_bytes(),
                                 (args.work / 'routed_checkpoint.odb').read_bytes())
                stages = [stage['name'] for stage in report['stages']]
                self.assertLess(stages.index('global-route-timing'), stages.index('detailed-route'))
                self.assertLess(stages.index('detailed-route'), stages.index('extract-routed'))
                self.assertLess(stages.index('extract-routed'), stages.index('postroute-vectors'))
                self.assertNotIn('route-extract', stages)
                self.assertEqual(set(report['global_checkpoint_sha256']), {'global_repaired.odb', 'global_repaired.sdc'})
            self.assertEqual(set(report['artifact_sha256']), expected_artifacts)
            self.assertFalse(any(stage['name'] in ('fill', 'drc', 'lvs') for stage in report['stages']))
            self.assertFalse((args.work / 'PAM5DriverControl.gds').exists())
            if resume_routed:
                self.assertEqual(report['resumed_routed_database']['sha256'], flow.digest(args.resume_routed_database))
                names = [stage['name'] for stage in report['stages']]
                self.assertIn('extract-checkpoint', names)
                self.assertNotIn('place-clock-tree', names)
                self.assertNotIn('route-extract', names)
                constraints = (args.work / 'constraints.sdc').read_text()
                self.assertIn('-period 8', constraints)
                self.assertIn('set_clock_uncertainty 0.1', constraints)
                self.assertEqual((args.work / 'routed_checkpoint.odb').read_bytes(), args.resume_routed_database.read_bytes())

    def test_retained_build_inputs_survive_rebuild_but_detect_copy_damage(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original, retained, design = root / 'mapped.v', root / 'retained.v', root / 'design.hpp'
            original.write_text('qualified netlist')
            retained.write_bytes(original.read_bytes())
            design.write_text('qualified source')
            expected = {str(path): flow.digest(path) for path in (original, design)}
            copies = {str(original): str(retained)}
            original.unlink()
            self.assertEqual(flow.changed_inputs(expected, copies), [])
            original.write_text('next regression')
            self.assertEqual(flow.changed_inputs(expected, copies), [])
            retained.write_text('damaged copy')
            self.assertEqual(flow.changed_inputs(expected, copies), [str(original)])
            retained.unlink()
            design.write_text('changed source')
            self.assertEqual(flow.changed_inputs(expected, copies), [str(original), str(design)])

    def test_changed_adc_array_interface_is_rejected_before_routing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            mapped = root / 'mapped'
            mapped.mkdir()
            source = root / 'source.hpp'
            source.write_text('qualified source')
            (mapped / 'mapped.v').write_text('qualified mapped netlist')
            (mapped / 'mapped.json').write_text(json.dumps({'modules': {'ADCReceiveController': {'ports': {}}}}))
            (mapped / 'mapped_interface.sv').write_text('changed array wrapper')
            (mapped / 'hardware_checks.json').write_text(json.dumps(dict(status='pass',
                native_mapped_exact=True, module='ADCReceiveController',
                mapped_sha256=flow.digest(mapped / 'mapped.v'), source_sha256={str(source): flow.digest(source)},
                simulation_interface_sha256='wrong hash')))
            python = root / 'pdk/.cpphdl/venv/bin/python'
            python.parent.mkdir(parents=True)
            python.touch()
            args = SimpleNamespace(module='ADCReceiveController', work=root / 'run', mapped_work=mapped,
                                   pdk=root / 'pdk', tools=root / 'tools')
            with self.assertRaisesRegex(ValueError, 'array-port simulation interface'):
                flow.implement(args)
            report = json.loads((args.work / 'physical_module.json').read_text())
            self.assertEqual(report['status'], 'failed')
            self.assertEqual(report['constraints']['period_ns'], 4)
            self.assertEqual(report['stages'], [])

    def test_resume_records_new_routing_only_code_but_rejects_placement_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            analog = root / 'analog'
            common = analog / 'pdk/flow'
            common.mkdir(parents=True)
            route, placement, fill = common / 'route_extract.tcl', common / 'place_route.tcl', common / 'fill_core.py'
            route.write_text('old routing')
            placement.write_text('old placement')
            fill.write_text('old fill')
            origin, work = root / 'origin', root / 'fresh'
            origin.mkdir()
            work.mkdir()
            for name in ('mapped.v', 'placed.odb', 'placed.sdc'):
                (origin / name).write_text('fixed ' + name)
            (work / 'mapped.v').write_text('fixed mapped.v')
            inputs = {str(path): flow.digest(path) for path in (route, placement, fill)}
            for path in (route, placement, fill):
                snapshot = origin / 'source_snapshot' / path.relative_to(root)
                snapshot.parent.mkdir(parents=True, exist_ok=True)
                snapshot.write_bytes(path.read_bytes())
            (origin / 'physical_module.json').write_text(json.dumps(dict(top='ReceiveFilter', constraints={},
                                                                       input_sha256=inputs)))
            route.write_text('new routing with checkpoints')
            fill.write_text('new postroute density repair')
            with patch.object(flow, 'ANALOG', analog), patch.object(flow, 'COMMON', common):
                result = flow.import_placement(origin, work, 'ReceiveFilter', {})
                self.assertEqual(result['changed_driver_sources'][str(route)]['scope'], 'routing-only')
                self.assertEqual(result['changed_driver_sources'][str(fill)]['scope'], 'postroute geometry')
                placement.write_text('changed placement algorithm')
                with self.assertRaisesRegex(ValueError, 'input changed'):
                    flow.import_placement(origin, work, 'ReceiveFilter', {})

    def test_resume_rejects_changed_design_inputs_and_checkpoint(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            origin, work = root / 'origin', root / 'work'
            origin.mkdir()
            work.mkdir()
            source = root / 'source.hpp'
            source.write_text('original')
            for name in ('mapped.v', 'placed.odb', 'placed.sdc'):
                (origin / name).write_text('original ' + name)
            (work / 'mapped.v').write_text('original mapped.v')
            contract = dict(period_ns=8)
            evidence = dict(top='ReceiveFilter', constraints=contract,
                            input_sha256={str(source): flow.digest(source)},
                            placement_checkpoint_sha256={name: flow.digest(origin / name)
                                                         for name in ('placed.odb', 'placed.sdc')})
            (origin / 'physical_module.json').write_text(json.dumps(evidence))
            result = flow.import_placement(origin, work, 'ReceiveFilter', contract)
            self.assertTrue(result['recorded_before_interruption'])
            self.assertEqual((work / 'placed.odb').read_bytes(), (origin / 'placed.odb').read_bytes())
            source.write_text('changed')
            with self.assertRaisesRegex(ValueError, 'input changed'):
                flow.import_placement(origin, work, 'ReceiveFilter', contract)
            source.write_text('original')
            (origin / 'placed.odb').write_text('different geometry')
            with self.assertRaisesRegex(ValueError, 'artifact changed'):
                flow.import_placement(origin, work, 'ReceiveFilter', contract)

    def test_failed_or_other_module_evidence_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            mapped = root / 'mapped'
            mapped.mkdir()
            for index, evidence in enumerate(({'status': 'fail'},
                    {'status': 'pass', 'native_frontend_exact': True, 'native_mapped_exact': True,
                     'module': 'RepeaterCore'})):
                (mapped / 'hardware_checks.json').write_text(json.dumps(evidence))
                work = root / str(index)
                args = SimpleNamespace(work=work, mapped_work=mapped, pdk=root / 'no-pdk', tools=root / 'no-tools')
                with self.assertRaisesRegex(ValueError, 'absent, failed or stale'):
                    flow.implement(args)
                report = json.loads((work / 'physical_module.json').read_text())
                self.assertEqual(report['status'], 'failed')
                self.assertFalse(report['full_1000base_t_chip'])
                self.assertEqual(report['stages'], [])
                self.assertFalse((work / 'PipelinedReceiveFilter.gds').exists())

    def test_existing_work_cannot_reuse_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            previous = work / 'physical_module.json'
            previous.write_text('{"status":"old"}')
            with self.assertRaisesRegex(ValueError, 'must be fresh'):
                flow.implement(SimpleNamespace(work=work, mapped_work=work))
            self.assertEqual(json.loads(previous.read_text())['status'], 'old')

    def test_fast_normalizer_keeps_its_timing_contract_when_evidence_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            mapped = root / 'mapped'
            mapped.mkdir()
            (mapped / 'hardware_checks.json').write_text('{"status":"failed"}')
            args = SimpleNamespace(module='ReceiveSampleNormalizer', work=root / 'run', mapped_work=mapped,
                                   pdk=root / 'no-pdk', tools=root / 'no-tools')
            with self.assertRaisesRegex(ValueError, 'absent, failed or stale'):
                flow.implement(args)
            report = json.loads((args.work / 'physical_module.json').read_text())
            self.assertEqual(report['constraints']['period_ns'], 4)
            self.assertEqual(report['status'], 'failed')
            self.assertFalse(report['full_1000base_t_chip'])
            self.assertEqual(report['stages'], [])

    def test_timing_recheck_rejects_changed_masks_and_cpp(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = ('postroute.v', 'core.spef', 'module_filled.gds')
            for name in inputs:
                (root / name).write_text('original ' + name)
            evidence = dict(status='failed', top='ReceiveTimingLoop', native_postroute_exact=True,
                            artifact_sha256={name: flow.digest(root / name) for name in inputs})
            (root / 'physical_module.json').write_text(json.dumps(evidence))
            args = SimpleNamespace(implementation=root, work=root / 'recheck')
            (root / 'module_filled.gds').write_text('changed mask')
            with self.assertRaisesRegex(ValueError, 'changed or untracked implementation artifact'):
                timing.recheck(args)
            (root / 'module_filled.gds').write_text('original module_filled.gds')
            source = root / 'module.hpp'
            source.write_text('old C++')
            proof = dict(status='pass', native_mapped_exact=True, module='ReceiveTimingLoop',
                         source_sha256={str(source): flow.digest(source)})
            (root / 'hardware_checks.json').write_text(json.dumps(proof))
            source.write_text('changed C++')
            with self.assertRaisesRegex(ValueError, r'C\+\+ module/harness source changed'):
                timing.recheck(args)
            self.assertFalse(args.work.exists())


class DieSizeContract(unittest.TestCase):
    def test_bad_dimensions_are_rejected_before_running_tools(self):
        from types import SimpleNamespace
        for value in (0, 40, -1, float('nan'), float('inf'), True, '360'):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'die size'):
                flow.implement(SimpleNamespace(module='InterleavedSARBankController', die_size=value))


if __name__ == '__main__':
    unittest.main()
