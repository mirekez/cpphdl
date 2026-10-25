#!/usr/bin/env python3
"""Offline script regression using a synthetic Git fixture and data-only setup."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


PDK = Path(__file__).resolve().parents[1]
REMOTE = "https://github.com/IHP-GmbH/IHP-Open-PDK.git"


class PrepareTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cpphdl-ihp-test-")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.source = self.work / "fixture"
        self.source.mkdir()
        self.destination = self.work / "kit's directory"
        # Redirect only this subprocess's Git requests; never contact the network.
        self.environment = dict(os.environ, GIT_CONFIG_COUNT="1",
                                GIT_CONFIG_KEY_0="url." + self.source.as_uri() + ".insteadOf",
                                GIT_CONFIG_VALUE_0=REMOTE, GIT_TERMINAL_PROMPT="0")
        for name in ("LICENSE", "requirements.txt", ".gitmodules",
                     "ihp-sg13g2/libs.tech/klayout/tech/sg13g2.lyt",
                     "ihp-sg13g2/libs.tech/klayout/tech/drc/run_drc.py",
                     "ihp-sg13g2/libs.tech/klayout/tech/lvs/run_lvs.py",
                     "ihp-sg13g2/libs.tech/ngspice/.spiceinit",
                     "ihp-sg13g2/libs.tech/ngspice/models/cornerMOSlv.lib"):
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("" if name == ".gitmodules" else "Synthetic setup fixture\n")
        self.git("init", "-q", cwd=self.source)
        self.git("config", "user.name", "Offline fixture", cwd=self.source)
        self.git("config", "user.email", "fixture@example.invalid", cwd=self.source)
        self.git("add", ".", cwd=self.source)
        self.git("commit", "-qm", "Synthetic fixture", cwd=self.source)
        self.revision = self.git("rev-parse", "HEAD", cwd=self.source)

    def git(self, *args, cwd=None):
        return subprocess.check_output(["git", *args], cwd=cwd, text=True,
                                       stderr=subprocess.PIPE).strip()

    def prepare(self, *extra, success=True):
        result = subprocess.run(
            ["bash", str(PDK / ".prepare_IHPSG13G2.sh"), "--destination", str(self.destination),
             "--revision", self.revision, "--python", sys.executable,
             "--skip-python", "--skip-models", *extra],
            env=self.environment, text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def test_clone_repeat_and_environment_quoting(self):
        result = self.prepare()
        self.assertIn("Partial setup", result.stdout)
        self.assertEqual(self.git("rev-parse", "HEAD", cwd=self.destination), self.revision)
        self.prepare()
        environment = subprocess.check_output(
            ["bash", "-c", 'source "$1"; printf "%s\\n%s" "$PDK_ROOT" "$PDK"',
             "ihp-test", str(self.destination / "env.sh")], text=True)
        self.assertEqual(environment, str(self.destination.resolve()) + "\nihp-sg13g2")

    def test_revision_mismatch_preserves_checkout(self):
        self.prepare()
        self.prepare("--revision", "0" * 40, success=False)
        self.assertEqual(self.git("rev-parse", "HEAD", cwd=self.destination), self.revision)

    def test_tracked_changes_are_preserved(self):
        self.prepare()
        license_file = self.destination / "LICENSE"
        license_file.write_text("User modification\n")
        self.prepare(success=False)
        self.assertEqual(license_file.read_text(), "User modification\n")

    def test_unrelated_destination_is_preserved(self):
        self.destination.mkdir()
        sentinel = self.destination / "keep"
        sentinel.write_text("keep\n")
        self.prepare(success=False)
        self.assertEqual(sentinel.read_text(), "keep\n")

    def test_failed_fetch_cleans_temporary_clone(self):
        self.prepare("--revision", "0" * 40, success=False)
        self.assertFalse(self.destination.exists())
        self.assertFalse(list(self.work.glob(".IHPSG13G2-clone.*")))

    def test_missing_required_file_is_reported(self):
        self.prepare()
        path = self.destination / "ihp-sg13g2/libs.tech/ngspice/.spiceinit"
        path.write_text("")
        # A staged, clean release fixture with a required empty file.
        self.git("config", "user.name", "Offline fixture", cwd=self.destination)
        self.git("config", "user.email", "fixture@example.invalid", cwd=self.destination)
        self.git("add", ".", cwd=self.destination)
        self.git("commit", "-qm", "Broken fixture", cwd=self.destination)
        revision = self.git("rev-parse", "HEAD", cwd=self.destination)
        result = self.prepare("--revision", revision, success=False)
        self.assertIn("Incomplete PDK", result.stderr)

    def test_invalid_options_fail_before_clone(self):
        for args in (("--revision", "dev"), ("--invalid",), ("--destination",)):
            with self.subTest(args=args):
                self.prepare(*args, success=False)
                self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
