#!/usr/bin/env python3
"""Exercise causal checkpoints without an expensive transistor simulation."""
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(SOURCE), str(SOURCE.parent / 'tests'), str(SOURCE.parents[1] / 'VerilogA')]
spec = importlib.util.spec_from_file_location('checkpointed_psp_engine', SOURCE / 'psp_live.py')
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)

SERVER = '''#!/usr/bin/env python3
import json,sys
from pathlib import Path
if sys.argv[1] != 'server':
    Path(sys.argv[1], 'live.json').write_text(json.dumps(dict(feedback_low_V=.36, feedback_high_V=.84,
        control_transition_s=1e-10, period_s=4e-9, clocks=4, launches=1,
        drivers=[dict(id=i, slice=0, bit=i) for i in range(9)], decisions=['decision'])))
else:
    print('clock,data,valid,slice,fault,acquire,convert,trial0', flush=True)
    print('-1,0,0,0,0,0,0,0', flush=True)
    for clock,line in enumerate(sys.stdin):
        feedback,launch=map(int,line.split())
        print(f'{clock},{feedback},{int(clock == 3)},0,0,0,{int(clock < 2)},{feedback}', flush=True)
'''


class Session:
    fail = False
    commands = []

    def __init__(self, *args):
        self.time = 0

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def command(self, text, timeout=None):
        self.commands.append((text, timeout))
        if '\nremcirc\n' in text:
            if self.fail:
                raise TimeoutError('injected simulator interruption')
            self.time = float(re.search(r'\ntran \S+ (\S+)', text)[1])

    def last(self, node):
        return self.time if node == 'time' else .95


def prepare(work, metadata):
    sources = '\n'.join(f'Vbit{i} bit{i} 0 PWL(0 0 1 0)' for i in range(9))
    return sources + '\n.control\nset wr_singlescale\ntran 1e-12 16e-9\nwrdata spice.dat v(decision)\n.endc\n.end\n', []


class Checkpoints(unittest.TestCase):
    def test_invalid_prefix_timeouts_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            args = self.setup_experiment(work)
            for timeout in (0, -1, float('inf'), float('nan')):
                args.prefix_timeout = timeout
                with self.subTest(timeout=timeout), self.assertRaisesRegex(ValueError, 'timeout'):
                    engine.live_flow(args, work, prepare)

    def setup_experiment(self, root):
        bench = root / 'bench'
        bench.write_text(SERVER)
        bench.chmod(0o755)
        return SimpleNamespace(bench=bench, slices=1, bursts=1, ngspice=root / 'unused',
                               checkpoint_fingerprint='source-and-model-digest', resume=False, prefix_timeout=35)

    def test_interrupted_prefix_resumes_identical_controller_and_bounds_memory(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            args = self.setup_experiment(work)
            Session.commands = []
            Session.fail = True
            with patch.object(engine, 'NgspiceSession', Session):
                with self.assertRaisesRegex(TimeoutError, 'injected'):
                    engine.live_flow(args, work, prepare)
                saved = json.loads((work / 'checkpoint.json').read_text())
                self.assertEqual(len(saved['decisions']), 1)
                Session.fail = False
                args.resume = True
                engine.live_flow(args, work, prepare)
            complete = json.loads((work / 'checkpoint.json').read_text())
            self.assertEqual(len(complete['decisions']), 4)
            self.assertEqual(complete['decisions'][:1], saved['decisions'])
            self.assertEqual(complete['controls'][:2], saved['controls'])
            self.assertEqual(complete['decisions'][1]['feedback'], 1)
            prefixes = [(command, timeout) for command, timeout in Session.commands if '\nremcirc\n' in command]
            self.assertTrue(prefixes)
            self.assertTrue(all(command.startswith('destroy all\nremcirc\nsource prefix.cir\n') and timeout == 35
                                for command, timeout in prefixes))

    def test_changed_source_metadata_or_controller_state_cannot_resume(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            args = self.setup_experiment(work)
            Session.fail = False
            with patch.object(engine, 'NgspiceSession', Session):
                engine.live_flow(args, work, prepare)
                saved = json.loads((work / 'checkpoint.json').read_text())
                args.resume = True
                for field in ('fingerprint', 'metadata', 'controls'):
                    changed = json.loads(json.dumps(saved))
                    if field == 'fingerprint':
                        changed[field] = 'different-source'
                    elif field == 'metadata':
                        changed[field]['period_s'] *= 2
                    else:
                        changed[field][2]['data'] = 99
                    (work / 'checkpoint.json').write_text(json.dumps(changed))
                    with self.subTest(corruption=field), self.assertRaisesRegex(ValueError, 'checkpoint'):
                        engine.live_flow(args, work, prepare)


if __name__ == '__main__':
    unittest.main()
