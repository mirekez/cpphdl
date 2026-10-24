"""Offline regressions: immutable checkouts, safe upgrades, and early Sail checks."""

import contextlib
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import ensure_git_repo as ensure
import riscv_dependencies as deps
import run_riscv_arch_test as arch


class CheckoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="riscv-dependency-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.origin = self.root / "origin"
        subprocess.run(["git", "init", str(self.origin)], check=True, capture_output=True)
        self.git("config", "user.name", "Dependency test")
        self.git("config", "user.email", "test@example.invalid")
        (self.origin / "sentinel").write_text("locked\n")
        self.git("add", "sentinel")
        self.git("commit", "-m", "locked")
        self.locked = self.git("rev-parse", "HEAD")
        (self.origin / "sentinel").write_text("moving branch\n")
        self.git("commit", "-am", "new default")
        self.latest = self.git("rev-parse", "HEAD")
        self.checkout = self.root / "checkout"

    def git(self, *args):
        return ensure.git(self.origin, *args)

    def clone(self):
        subprocess.run(["git", "clone", str(self.origin), str(self.checkout)],
                       check=True, capture_output=True)

    def test_fresh_checkout_uses_pin_not_remote_head(self):
        ensure.ensure_checkout(self.checkout, str(self.origin), "sentinel", self.locked)
        self.assertEqual(ensure.git(self.checkout, "rev-parse", "HEAD"), self.locked)
        self.assertEqual((self.checkout / "sentinel").read_text(), "locked\n")
        self.assertNotEqual(self.locked, self.latest)

    def test_existing_wrong_revision_fails_even_with_sentinel(self):
        self.clone()
        with self.assertRaisesRegex(ValueError, "Dependency revision mismatch"):
            ensure.ensure_checkout(self.checkout, str(self.origin), "sentinel", self.locked)
        self.assertEqual(ensure.git(self.checkout, "rev-parse", "HEAD"), self.latest)

    def test_loader_can_select_pin_on_clean_checkout(self):
        self.clone()
        ensure.ensure_checkout(self.checkout, str(self.origin), "sentinel", self.locked, update=True)
        self.assertEqual(ensure.git(self.checkout, "rev-parse", "HEAD"), self.locked)

    def test_dirty_mismatched_checkout_is_not_modified(self):
        self.clone()
        (self.checkout / "sentinel").write_text("user edits\n")
        with self.assertRaisesRegex(ValueError, "Refusing to change dirty dependency"):
            ensure.ensure_checkout(self.checkout, str(self.origin), "sentinel", self.locked, update=True)
        self.assertEqual((self.checkout / "sentinel").read_text(), "user edits\n")
        self.assertEqual(ensure.git(self.checkout, "rev-parse", "HEAD"), self.latest)

    def test_expected_base_keeps_riscv_dv_compatibility_edits(self):
        ensure.ensure_checkout(self.checkout, str(self.origin), "sentinel", self.locked)
        (self.checkout / "sentinel").write_text("compatibility patch\n")
        ensure.ensure_checkout(self.checkout, str(self.origin), "sentinel", self.locked, update=True)
        self.assertEqual((self.checkout / "sentinel").read_text(), "compatibility patch\n")

    def test_non_git_sentinel_is_not_accepted(self):
        self.checkout.mkdir()
        (self.checkout / "sentinel").touch()
        with self.assertRaisesRegex(ValueError, "not an independent Git checkout"):
            ensure.ensure_checkout(self.checkout, str(self.origin), "sentinel", self.locked)

    def test_branch_name_is_not_a_pin(self):
        with self.assertRaisesRegex(ValueError, "full immutable Git SHA"):
            ensure.ensure_checkout(self.checkout, str(self.origin), "sentinel", "main")

    def test_parallel_first_clone_is_serialized(self):
        script = ("import sys; from pathlib import Path; sys.path.insert(0, sys.argv[1]); "
                  "from ensure_git_repo import ensure_checkout; "
                  "ensure_checkout(Path(sys.argv[2]), sys.argv[3], 'sentinel', sys.argv[4])")
        command = [sys.executable, "-B", "-c", script, str(pathlib.Path(ensure.__file__).parent),
                   str(self.checkout), str(self.origin), self.locked]
        processes = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(2)]
        for process in processes:
            output, error = process.communicate(timeout=30)
            self.assertEqual(process.returncode, 0, (output, error))
        self.assertEqual(ensure.git(self.checkout, "rev-parse", "HEAD"), self.locked)


class ReferenceModelTests(unittest.TestCase):
    def executable(self, directory, name, body):
        path = directory / name
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(0o755)
        return path

    def test_locked_model_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            model = self.executable(pathlib.Path(directory), "sail", "echo 0.13.1")
            self.assertEqual(deps.check_sail_version(str(model)), "0.13.1")

    def test_old_or_new_incompatible_model_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            for version in ("0.11", "0.16"):
                model = self.executable(pathlib.Path(directory), "sail", "echo " + version)
                with self.assertRaisesRegex(RuntimeError, "version mismatch.*0.13.1"):
                    deps.check_sail_version(str(model))

    def test_arch_runner_checks_model_before_verilator(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            tools = root / "bin"
            tools.mkdir()
            self.executable(tools, "sail_riscv_sim", "echo 0.11")
            for tool in ("riscv32-unknown-elf-gcc", "riscv32-unknown-elf-objdump", "riscv32-unknown-elf-readelf"):
                self.executable(tools, tool, "echo test-tool")
            self.executable(tools, "riscv32-unknown-elf-gcc",
                            'if [ "$1" = "-print-prog-name=as" ]; then echo /bin/true; else echo test-gcc; fi')
            tribe = self.executable(root, "tribe", "exit 99")
            checkout = root / "arch"
            checkout.mkdir()
            actual_run = subprocess.run

            def run(command, *args, **kwargs):
                if len(command) > 1 and str(command[1]).endswith("ensure_git_repo.py"):
                    return subprocess.CompletedProcess(command, 0)
                return actual_run(command, *args, **kwargs)

            environment = {"RISCV_HOME": str(root), "TRIBE_ARCH_TEST_BACKEND": "verilator",
                           "TRIBE_ARCH_TEST_REF_MODEL": "sail_riscv_sim"}
            output = io.StringIO()
            with patch.dict(os.environ, environment), patch.object(subprocess, "run", side_effect=run), \
                 patch.object(arch, "select_uv_python"), patch.object(arch, "prewarm_udb_z3_cache"), \
                 patch.object(arch, "run", side_effect=AssertionError("RTL build must not start")), \
                 contextlib.redirect_stdout(output):
                self.assertEqual(arch.main(["runner", str(tribe), str(checkout), str(root / "work")]), 1)
            self.assertIn("reports '0.11'", output.getvalue())
            self.assertIn(".load_sail_riscv_sim.sh", output.getvalue())

    def test_revision_changes_invalidate_generated_work(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            work = root / "work"
            checkout = root / "checkout"
            with patch.object(arch, "checkout_revision", return_value="first"):
                arch.prepare_work_dir(work, checkout)
                cached = work / "old.elf"
                cached.touch()
                arch.prepare_work_dir(work, checkout)
                self.assertTrue(cached.exists())
            with patch.object(arch, "checkout_revision", return_value="second"):
                arch.prepare_work_dir(work, checkout)
                self.assertFalse(cached.exists())

    def test_manifest_contains_immutable_repository_revisions(self):
        for entry in deps.load_lock()["repositories"].values():
            self.assertRegex(entry["revision"], r"^[0-9a-f]{40}$")


if __name__ == "__main__":
    unittest.main()
