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
