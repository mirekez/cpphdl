#!/usr/bin/env python3
"""Check integration patch paths and upgrades without fetching external tools."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest
from version_fixture import configure_product, commit

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
        configure_product(product, checkout)
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

    def test_wrong_revision_stops_before_patching(self):
        repo, checkout, product = self.fixture()
        (checkout / 'other-version').write_text('new version\n')
        revision = commit(checkout)
        result = self.install(checkout, product)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('required', result.stderr)
        self.assertEqual((checkout / 'build.sbt').read_text(), 'original\n')
        self.assertEqual(self.git(repo, 'rev-parse', 'HEAD').stdout.strip(), revision)
        self.assertFalse((checkout / 'created').exists())

    def test_circt_branch_override_stops_before_patching(self):
        repo, checkout, product = self.fixture()
        result = subprocess.run(['bash', str(product / 'install.sh'), str(checkout)],
                                env={**os.environ, 'CIRCT_CPPHDL_COMMIT': 'master'},
                                text=True, capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('locked revision', result.stderr)
        self.assertEqual((checkout / 'build.sbt').read_text(), 'original\n')

    def test_dirty_exporter_does_not_bypass_circt_revision(self):
        repo, checkout, product = self.fixture()
        circt = self.base / 'circt'
        circt.mkdir()
        self.git(circt, 'init', '-q')
        for path in ('include/circt/Conversion/Exportcpphdl.h',
                     'lib/Conversion/Exportcpphdl/Exportcpphdl.cpp', 'tools/firtool/firtool.cpp'):
            target = circt / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('"split-cpphdl"\n')
        pinned = commit(circt)
        lock = json.loads((product / 'revisions.json').read_text())
        lock['circt']['commit'] = pinned
        (product / 'revisions.json').write_text(json.dumps(lock))
        (circt / 'other-revision').write_text('new revision\n')
        actual = commit(circt)
        (circt / 'tools/firtool/firtool.cpp').write_text('"split-cpphdl" // local changes\n')
        result = subprocess.run(['bash', str(product / 'install.sh'), str(checkout)],
                                env={**os.environ, 'CIRCT_CPPHDL_SOURCE_DIR': str(circt)},
                                text=True, capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(pinned, result.stderr)
        self.assertEqual(self.git(circt, 'rev-parse', 'HEAD').stdout.strip(), actual)
        self.assertEqual((checkout / 'build.sbt').read_text(), 'original\n')

    def test_exporter_lowercase_paths(self):
        repo = self.base / 'exporter'
        repo.mkdir()
        self.git(repo, 'init', '-q')
        self.git(repo, 'apply', '--include=include/circt/Conversion/Exportcpphdl.h',
                 '--include=lib/Conversion/Exportcpphdl/*', ROOT / 'firrtl_cpphdl.patch')
        header = repo / 'include/circt/Conversion/Exportcpphdl.h'
        source = repo / 'lib/Conversion/Exportcpphdl/Exportcpphdl.cpp'
        self.assertIn('exportcpphdl(', header.read_text())
        self.assertIn('circt::exportcpphdl(', source.read_text())
        self.assertTrue(source.read_text().endswith('  return success();\n}\n'),
                        'Patch hunk length must not truncate the exporter')
        self.assertIn('CIRCTExportcpphdl',
                      (source.parent / 'CMakeLists.txt').read_text())
        self.git(repo, 'apply', '--reverse', '--check',
                 '--include=include/circt/Conversion/Exportcpphdl.h',
                 '--include=lib/Conversion/Exportcpphdl/*', ROOT / 'firrtl_cpphdl.patch')

    def test_exporter_detection_preserves_existing_checkouts(self):
        repo, checkout, product = self.fixture()
        self.git(repo, 'apply', '--include=tools/firtool-cpphdl/CMakeLists.txt',
                 ROOT / 'chipyard_cpphdl.patch')
        cmake = (repo / 'tools/firtool-cpphdl/CMakeLists.txt').read_text()
        discovery = 'file(GLOB CPPHDL_EXPORT_SOURCE ' + cmake.split(
            'file(GLOB CPPHDL_EXPORT_SOURCE ', 1)[1].split('add_executable', 1)[0]
        script = (product / 'install.sh').read_text().split(
            '[[ -f "$chipyard_root/build.sbt" ]]')[0]
        script += '\nhas_cpphdl_exporter "$CIRCT_CPPHDL_SOURCE_DIR"\n'
        (product / 'detect.sh').write_text(script)
        for suffix in ('cpphdl', 'cpp'.capitalize() + 'hdl'.upper()):
            with self.subTest(suffix=suffix):
                circt = self.base / suffix
                header = circt / f'include/circt/Conversion/Export{suffix}.h'
                source = circt / f'lib/Conversion/Export{suffix}/Export{suffix}.cpp'
                tool = circt / 'tools/firtool/firtool.cpp'
                for path in (header, source, tool):
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text('')
                tool.write_text('"split-cpphdl"\n')
                result = subprocess.run(['bash', str(product / 'detect.sh'), str(checkout)],
                                        env={**os.environ, 'CIRCT_CPPHDL_SOURCE_DIR': str(circt)},
                                        text=True, capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                # Exercise the installed template's source discovery without an SDK.
                probe = product / 'source.cmake'
                probe.write_text(f'set(CIRCT_CPPHDL_SOURCE_DIR "{circt}")\n' + discovery +
                                 'message("selected=${CPPHDL_EXPORT_SOURCE}")\n')
                result = subprocess.run(['cmake', '-P', str(probe)], text=True,
                                        capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f'selected={source}', result.stderr)
                source.unlink()
                result = subprocess.run(['bash', str(product / 'detect.sh'), str(checkout)],
                                        env={**os.environ, 'CIRCT_CPPHDL_SOURCE_DIR': str(circt)},
                                        text=True, capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)

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
