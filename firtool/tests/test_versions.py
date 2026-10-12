#!/usr/bin/env python3
"""Reject moving or missing repositories without altering prepared checkouts."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from version_fixture import ROOT, commit, configure_product, git


class Versions(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cpphdl pinned versions ')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.product = self.base / 'scripts'
        self.root = self.base / 'checkout'
        self.child = self.root / 'generators/core'
        self.leaf = self.child / 'dependencies/library'
        for repo in (self.root, self.child, self.leaf):
            repo.mkdir(parents=True)
            subprocess.run(['git', '-C', str(repo), 'init', '-q'], check=True)
            (repo / 'source').write_text('pinned source\n')
        commit(self.leaf)
        commit(self.child)
        commit(self.root)
        self.lock = configure_product(self.product, self.root)
        self.lock['chipyard']['required_submodules'] = ['generators/core']
        self.save_lock()

    def save_lock(self):
        (self.product / 'revisions.json').write_text(json.dumps(self.lock))

    def check(self, root=None, component='chipyard'):
        return subprocess.run(['python3', str(self.product / 'check_versions.py'),
                               str(root or self.root), '--component', component],
                              text=True, capture_output=True, timeout=10,
                              env={**os.environ, 'ALLOW_UNPINNED_CHIPYARD': '1'})

    def drift(self, repo):
        path = repo / 'drift'
        path.write_text((path.read_text() if path.exists() else '') + 'moving branch\n')
        return commit(repo)

    def test_current_branch_at_pin_is_accepted(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('3 repositories', result.stdout)

    def test_wrong_root_is_rejected_and_preserved(self):
        revision = self.drift(self.root)
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(self.lock['chipyard']['commit'], result.stderr)
        self.assertEqual(git(self.root, 'rev-parse', 'HEAD'), revision)

    def test_dirty_tracked_source_at_pin_is_preserved(self):
        (self.child / 'source').write_text('local extension\n')
        git(self.child, 'add', 'source')
        before = git(self.child, 'status', '--porcelain')
        self.assertEqual(self.check().returncode, 0)
        self.assertEqual(git(self.child, 'status', '--porcelain'), before)

    def test_direct_submodule_drift_cannot_be_hidden_in_index(self):
        revision = self.drift(self.child)
        git(self.root, 'update-index', '--cacheinfo', f'160000,{revision},generators/core')
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(self.child), result.stderr)
        self.assertEqual(git(self.child, 'rev-parse', 'HEAD'), revision)

    def test_nested_drift_is_rejected(self):
        revision = self.drift(self.leaf)
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(self.leaf), result.stderr)
        self.assertEqual(git(self.leaf, 'rev-parse', 'HEAD'), revision)

    def test_explicit_override_is_accepted_and_still_checked(self):
        revision = self.drift(self.child)
        self.lock['chipyard']['overrides']['generators/core'] = revision
        self.save_lock()
        self.assertEqual(self.check().returncode, 0)
        self.drift(self.child)
        self.assertNotEqual(self.check().returncode, 0)

    def test_missing_required_submodule_is_rejected(self):
        shutil.rmtree(self.child)
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not initialized', result.stderr)

    def test_missing_nested_required_dependency_is_rejected(self):
        shutil.rmtree(self.leaf)
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not initialized', result.stderr)
        self.assertIn(str(self.leaf), result.stderr)

    def test_absent_optional_submodule_is_allowed(self):
        self.lock['chipyard']['required_submodules'] = []
        self.save_lock()
        shutil.rmtree(self.child)
        (self.child / 'empty-scaffold').mkdir(parents=True)
        self.assertEqual(self.check().returncode, 0)

    def test_optional_unversioned_sources_are_rejected(self):
        self.lock['chipyard']['required_submodules'] = []
        self.save_lock()
        shutil.rmtree(self.child / '.git')
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no Git checkout', result.stderr)

    def test_dirty_circt_at_wrong_revision_is_rejected(self):
        self.lock['circt']['commit'] = git(self.child, 'rev-parse', 'HEAD')
        self.save_lock()
        self.drift(self.child)
        (self.child / 'source').write_text('patched exporter\n')
        result = self.check(self.child, 'circt')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('required', result.stderr)

    def test_branch_name_is_not_a_lock_revision(self):
        self.lock['circt']['commit'] = 'master'
        self.save_lock()
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('full commit SHAs', result.stderr)

    def test_sdk_version_is_verified_before_reuse(self):
        sdk = self.base / 'sdk'
        for package, name in (('circt', 'CIRCT'), ('mlir', 'MLIR'), ('llvm', 'LLVM')):
            path = sdk / 'lib/cmake' / package / f'{name}Config.cmake'
            path.parent.mkdir(parents=True)
            path.touch()
        tool = sdk / 'bin/firtool'
        tool.parent.mkdir()
        tool.write_text('#!/bin/sh\necho "CIRCT firtool-1.75.0"\n')
        tool.chmod(0o755)
        shutil.copy2(ROOT / '.bootstrap_circt_sdk.sh', self.product / '.bootstrap_circt_sdk.sh')
        script = self.product / '.bootstrap_circt_sdk.sh'
        good = subprocess.run(['bash', str(script), str(sdk)], text=True, capture_output=True, timeout=10)
        self.assertEqual(good.returncode, 0, good.stderr)
        tool.write_text('#!/bin/sh\necho "CIRCT firtool-1.76.0"\n')
        bad = subprocess.run(['bash', str(script), str(sdk)], text=True, capture_output=True, timeout=10)
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn('firtool-1.75.0', bad.stderr)
        self.assertFalse((self.product / '.cache').exists())
        self.assertIn('1.76.0', tool.read_text())


if __name__ == '__main__':
    unittest.main()
