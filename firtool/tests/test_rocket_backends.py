#!/usr/bin/env python3
"""Check public build entry points without rebuilding Rocket."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
class Backends(unittest.TestCase):
    def test_run_config_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            shutil.copy2(ROOT / '.run_rocket64_cpphdl.sh', base / 'run.sh')
            elf = base / 'matrix.elf'
            elf.write_text('fixture')
            for backend, subdir, target in [
                ('plain', 'plain', 'cpphdl-rocket64-sim'),
                ('optimize-combs', '', 'cpphdl-rocket64-optimized-sim'),
                ('native-graph', 'native-graph', 'cpphdl-rocket64-graph-sim')]:
                with self.subTest(backend=backend):
                    mode = base / 'chipyard/cpphdl-build/OtherConfig' / subdir
                    (mode / 'runtime').mkdir(parents=True)
                    sim = mode / 'runtime' / target
                    sim.write_text('#!/bin/sh\necho "ROCKET RV64 MMUL TEST PASSED: fixture"\n')
                    sim.chmod(0o755)
                    if backend == 'optimize-combs':
                        (mode / 'generated').mkdir()
                        (mode / 'generated/TestHarness_optimized_combs.cpp').write_text('// fixture\n')
                    env = {k: v for k, v in os.environ.items() if not k.startswith('CPPHDL_')}
                    env['CPPHDL_CONFIG'] = 'OtherConfig'
                    result = subprocess.run(['bash', str(base / 'run.sh'), backend, str(elf)],
                                            env=env, text=True, capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertTrue((mode / 'rocket64-mmul.log').is_file())

    def test_dispatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            product = base / 'firtool'
            product.mkdir()
            for path in ROOT.glob('.build_rocket64_cpphdl*.sh'):
                shutil.copy2(path, product / path.name)
            (base / 'include').mkdir()
            (base / 'include/cpphdl.h').touch()
            checkout = base / 'existing chipyard'
            (checkout / 'scripts').mkdir(parents=True)
            (product / 'chipyard').symlink_to(checkout)
            patch = product / '.chipyard_cpphdl_patch.sh'
            patch.write_text('#!/bin/sh\nexit 0\n')
            patch.chmod(0o755)
            build = checkout / 'scripts/build-cpphdl-rocket64.sh'
            build.write_text('#!/usr/bin/env python3\nimport json,os,sys\n'
                             'print(json.dumps([os.environ["CPPHDL_BACKEND"], '
                             'os.environ["CPPHDL_OPT_LEVEL"], sys.argv[1:]]))\n')
            build.chmod(0o755)
            for suffix, backend in [('', 'plain'), ('-optimize-combs', 'optimize-combs'), ('-native-graph', 'native-graph')]:
                with self.subTest(backend=backend):
                    env = {**os.environ, 'CPPHDL_TOOL': '/missing' if backend == 'plain' else '/bin/true',
                           'CPPHDL_BACKEND': 'wrong', 'CPPHDL_OPTIMIZE_COMBS': '1'}
                    env.pop('CPPHDL_OPT_LEVEL', None)
                    result = subprocess.run([str(product / ('.build_rocket64_cpphdl' + suffix + '.sh')), 'test.elf'],
                                            env=env, text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(json.loads(result.stdout), [backend, '-O2', ['test.elf']])

if __name__ == '__main__':
    unittest.main()
