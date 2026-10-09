#!/usr/bin/env python3
"""Check integration patch paths and upgrades without fetching external tools."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ChipyardPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cpphdl patch ')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)

    def git(self, repo, *args, **kwargs):
        return subprocess.run(['git', '-C', str(repo), *map(str, args)],
                              text=True, capture_output=True, check=True, **kwargs)

    def fixture(self, embedded=False):
        repo = self.base / 'repository'
        repo.mkdir()
        self.git(repo, 'init', '-q')
        checkout = repo / 'vendor/chip yard' if embedded else repo
        checkout.mkdir(parents=True, exist_ok=True)
        (checkout / 'build.sbt').write_text('original\n')
        product = self.base / 'installer'
        product.mkdir()
        # Stop before the download/build phase, leaving the real path and
        # idempotence logic intact. No Chipyard/CIRCT/network is needed.
        script = (ROOT / '.chipyard_cpphdl_patch.sh').read_text().split(
            'step "Reuse or fetch pinned CIRCT/firtool sources"')[0]
        (product / 'install.sh').write_text(script)
        patch = ('diff --git a/build.sbt b/build.sbt\n'
                 '--- a/build.sbt\n+++ b/build.sbt\n@@ -1 +1 @@\n'
                 '-original\n+updated\n'
                 'diff --git a/created b/created\nnew file mode 100644\n'
                 '--- /dev/null\n+++ b/created\n@@ -0,0 +1 @@\n+created\n')
        for name in ('chipyard_cpphdl.patch', 'chipyard_cpphdl_options.patch',
                     'firrtl_cpphdl.patch'):
            (product / name).write_text(patch)
        return repo, checkout, product

    def install(self, checkout, product):
        return subprocess.run(['bash', str(product / 'install.sh'), str(checkout)],
                              text=True, capture_output=True, timeout=10)

    def check_install(self, embedded):
        repo, checkout, product = self.fixture(embedded)
        for _ in range(2):
            result = self.install(checkout, product)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((checkout / 'build.sbt').read_text(), 'updated\n')
            self.assertEqual((checkout / 'created').read_text(), 'created\n')
        prefix = 'vendor/chip yard/' if embedded else ''
        exclude = (repo / '.git/info/exclude').read_text()
        self.assertEqual(exclude.count('/' + prefix + 'cpphdl-build/'), 1)
        ignored = self.git(repo, 'check-ignore', prefix + 'cpphdl-build/model').stdout
        self.assertIn('cpphdl-build/model', ignored)
        if embedded:
            self.assertFalse((repo / 'created').exists())
            self.assertNotIn('\n/cpphdl-build/', exclude)

    def test_standalone_repeat(self):
        self.check_install(False)

    def test_embedded_repeat(self):
        self.check_install(True)

    def test_conflict_is_atomic(self):
        repo, checkout, product = self.fixture(True)
        (checkout / 'build.sbt').write_text('user edit\n')
        result = self.install(checkout, product)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('does not apply cleanly', result.stderr)
        self.assertEqual((checkout / 'build.sbt').read_text(), 'user edit\n')
        self.assertFalse((checkout / 'created').exists())

    def test_options_upgrade_matches_fresh_install(self):
        repo = self.base / 'repository'
        repo.mkdir()
        self.git(repo, 'init', '-q')
        # Only these newly created files are needed to test the upgrade patch.
        paths = ['scripts/build-cpphdl-rocket64.sh',
                 'scripts/build-cpphdl-rocket64-graph.sh']
        self.git(repo, 'apply', *['--include=' + p for p in paths],
                 ROOT / 'chipyard_cpphdl.patch')
        fresh = {p: (repo / p).read_text() for p in paths}
        self.git(repo, 'apply', '--reverse', ROOT / 'chipyard_cpphdl_options.patch')
        self.assertIn('config=RocketConfig', (repo / paths[0]).read_text())
        self.assertNotIn('CPPHDL_EXTRA_SOURCES', (repo / paths[1]).read_text())
        self.git(repo, 'apply', ROOT / 'chipyard_cpphdl_options.patch')
        for p in paths:
            self.assertEqual((repo / p).read_text(), fresh[p])
            subprocess.run(['bash', '-n', str(repo / p)], check=True)
        self.git(repo, 'apply', '--reverse', '--check', ROOT / 'chipyard_cpphdl_options.patch')


if __name__ == '__main__':
    unittest.main()
