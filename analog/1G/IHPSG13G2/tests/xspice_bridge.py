#!/usr/bin/env python3
"""An event log must agree with the saved physical decision voltages and clock."""
import csv
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(SOURCE), str(SOURCE.parent / 'tests'), str(SOURCE.parents[1] / 'VerilogA')]
spec = importlib.util.spec_from_file_location('continuous_cpp_controller', SOURCE / 'xspice_live.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class BridgeEvidence(unittest.TestCase):
    def test_live_controls_keep_the_exported_edge(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            metadata = dict(feedback_low_V=.36, feedback_high_V=.84, period_s=4e-9,
                decisions=['decision0'], drivers=[dict(slice=0, bit=i, id=i) for i in range(9)])
            (work / 'live.json').write_text(json.dumps(metadata))
            args = SimpleNamespace(slices=1, bursts=2, bench=work / 'bench',
                ngspice=work / 'ngspice', cxx='c++')
            deck = ''.join(f'Vbit{i} bit{i} 0 PWL(0 1)\n' for i in range(9)) + '.control\nquit\n.endc\n.end\n'
            with patch.object(bridge, 'execute'), patch.object(bridge, 'headers', return_value=work):
                for edge in (2e-15, 100e-12):
                    (work / 'conditions.json').write_text(json.dumps(dict(driver_edge_s=edge)))
                    bridge.live_flow(args, work, lambda *_: (deck, {}))
                    output = (work / 'cosim.cir').read_text()
                    card = next(line for line in output.splitlines() if line.startswith('.model host_output '))
                    for parameter in ('t_rise', 't_fall'):
                        value = card.split(parameter + '=')[1].split()[0].rstrip(')')
                        self.assertEqual(float(value), edge)
                for edge in (None, True, 0, -1, float('inf'), float('nan'), '2fs'):
                    (work / 'conditions.json').write_text(json.dumps(dict(driver_edge_s=edge)))
                    with self.assertRaisesRegex(ValueError, 'exported driver edge'):
                        bridge.live_flow(args, work, lambda *_: (deck, {}))

    def test_bounded_runtime_budget(self):
        self.assertEqual(bridge.simulation_timeout(SimpleNamespace(slices=4)), 3600)
        self.assertEqual(bridge.simulation_timeout(SimpleNamespace(slices=16)), 9600)
        self.assertEqual(bridge.simulation_timeout(SimpleNamespace(slices=16, simulation_timeout=123)), 123)
        for value in (0, -1, float('inf'), float('nan')):
            with self.assertRaises(ValueError):
                bridge.simulation_timeout(SimpleNamespace(slices=16, simulation_timeout=value))

    def fixture(self, work):
        (work / 'live.json').write_text(json.dumps(dict(slices=1, clocks=25, period_s=4e-9)))
        (work / 'columns.csv').write_text('time_s,CORE_DECISION0\n')
        (work / 'spice.dat').write_text('time observation0\n' + '\n'.join(
            f'{i * 1e-9:.17g} 1.1' for i in range(101)) + '\n')
        with (work / 'controls.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['clock', 'convert'])
            writer.writerows((i, int(4 <= i < 12)) for i in range(-1, 25))
        with (work / 'bridge_events.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['clock', 'time_s', 'active_mask', 'feedback'])
            writer.writerows((i, (i + .5) * 4e-9 + 3e-12, int(5 <= i < 13), int(5 <= i < 13)) for i in range(25))

    def test_complete_independent_voltage_and_phase_check(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            self.fixture(work)
            result = bridge.check_bridge(work, 1)
            self.assertEqual(result['decision_reads'], 8)
            self.assertLessEqual(result['maximum_clock_phase_offset_s'], 5e-12)

    def test_delayed_clock_origin_and_reader_callback(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            self.fixture(work)
            origin = 16e-6
            metadata = json.loads((work / 'live.json').read_text())
            metadata['clock_origin_s'] = origin
            (work / 'live.json').write_text(json.dumps(metadata))
            rows = [[origin + i * 1e-9, 1.1] for i in range(101)]
            path = work / 'bridge_events.csv'
            with path.open() as stream:
                events = list(csv.DictReader(stream))
            for event in events:
                event['time_s'] = format(origin + float(event['time_s']), '.17g')
            with path.open('w') as stream:
                writer = csv.DictWriter(stream, ['clock', 'time_s', 'active_mask', 'feedback'])
                writer.writeheader()
                writer.writerows(events)
            (work / 'spice.dat').unlink()
            result = bridge.check_bridge(work, 1, lambda path, names: rows)
            self.assertEqual(result['decision_reads'], 8)
            with self.assertRaisesRegex(ValueError, 'trajectory'):
                bridge.check_bridge(work, 1, lambda path, names: [[r[0] - origin, r[1]] for r in rows])

    def test_repeated_time_analog_state_inside_aperture_is_checked(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            self.fixture(work)
            rows = [[i * 1e-9, 1.1] for i in range(101)]
            # Ten picoseconds after the event, between the three interpolated
            # probe times, a repeated simulator state is unresolved.
            time = 26e-9 + 13e-12
            rows += [[time, 1.1], [time, .6], [time, 1.1]]
            rows.sort(key=lambda row: row[0])
            with self.assertRaisesRegex(ValueError, 'unstable'):
                bridge.check_bridge(work, 1, lambda path, names: rows)

    def test_wrong_feedback_clock_phase_or_voltage_is_rejected(self):
        for corruption in ('feedback', 'phase', 'voltage', 'missing_clock'):
            with self.subTest(corruption=corruption), tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                self.fixture(work)
                with (work / 'bridge_events.csv').open() as stream:
                    events = list(csv.DictReader(stream))
                if corruption == 'feedback':
                    events[6]['feedback'] = '0'
                elif corruption == 'phase':
                    events[6]['time_s'] = str(26e-9 + 6e-12)
                elif corruption == 'missing_clock':
                    events.pop()
                else:
                    (work / 'spice.dat').write_text((work / 'spice.dat').read_text().replace(' 1.1\n', ' .6\n'))
                with (work / 'bridge_events.csv').open('w') as stream:
                    writer = csv.DictWriter(stream, ['clock', 'time_s', 'active_mask', 'feedback'])
                    writer.writeheader()
                    writer.writerows(events)
                with self.assertRaises(ValueError):
                    bridge.check_bridge(work, 1)


if __name__ == '__main__':
    unittest.main()
