"""Offline regressions: immutable checkouts, safe upgrades, and early Sail checks."""

import contextlib
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import ensure_git_repo as ensure
import riscv_dependencies as deps
import run_riscv_arch_test as arch
import run_riscv_tests as isa
import run_riscv_dv as dv
import run_spike_fragments as fragments


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

    def test_spike_conda_library_path_and_missing_soname(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            library = root / "conda" / "lib"
            library.mkdir(parents=True)
            source = root / "dependency.c"
            source.write_text("int dependency(void) { return 0; }\n")
            soname = "libcpphdl_spike_fixture.so.1"
            cc = shutil.which("cc")
            self.assertIsNotNone(cc, "C compiler required for runtime loader regression")
            subprocess.run([cc, "-shared", "-fPIC", str(source), "-Wl,-soname," + soname,
                            "-o", str(library / soname)], check=True)
            source.write_text("extern int dependency(void); int main(void) { return dependency(); }\n")
            model = root / "spike"
            subprocess.run([cc, str(source), str(library / soname), "-o", str(model)], check=True)
            env = os.environ.copy()
            env.pop("LD_LIBRARY_PATH", None)
            failed = subprocess.run([str(model), "--help"], env=env, capture_output=True)
            self.assertNotEqual(failed.returncode, 0)
            env["CONDA_PREFIX"] = str(library.parent)
            env["LD_LIBRARY_PATH"] = "/preserved/library/path"
            deps.prepare_spike_runtime(env, str(model))
            deps.prepare_spike_runtime(env, str(model))
            self.assertEqual(env["LD_LIBRARY_PATH"], str(library) + ":/preserved/library/path")
            (library / soname).rename(library / "different-version.so")
            with self.assertRaisesRegex(RuntimeError, "libcpphdl_spike_fixture.so.1"):
                deps.prepare_spike_runtime(env, str(model))

    def test_spike_runtime_preserves_non_conda_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            model = self.executable(pathlib.Path(directory), "spike", "exit 0")
            env = {"PATH": "/bin", "LD_LIBRARY_PATH": "/custom/lib"}
            expected = env.copy()
            deps.prepare_spike_runtime(env, str(model))
            self.assertEqual(env, expected)

    def test_spike_runtime_timeout_reports_rebuild(self):
        with patch.object(subprocess, "run", side_effect=subprocess.TimeoutExpired("spike", 10)):
            with self.assertRaisesRegex(RuntimeError, r"\.load_spike.sh"):
                deps.prepare_spike_runtime({"PATH": "/bin"})

    def test_all_spike_runners_fail_before_workloads(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            tools = root / "bin"
            tools.mkdir()
            for name in ("autoconf", "automake", "make", "riscv32-unknown-elf-gcc",
                         "riscv32-unknown-elf-readelf", "riscv32-unknown-elf-objcopy"):
                self.executable(tools, name, "exit 99")
            self.executable(tools, "spike", 'echo "missing libboost_regex.so.1.92.0" >&2; exit 127')
            tribe = self.executable(root, "tribe", "exit 99")
            actual_run = subprocess.run
            calls = []

            def run(command, *args, **kwargs):
                if len(command) > 1 and str(command[1]).endswith("ensure_git_repo.py"):
                    return subprocess.CompletedProcess(command, 0)
                calls.append(command)
                self.assertEqual(command, [str(tools / "spike"), "--help"])
                return actual_run(command, *args, **kwargs)

            for runner in (isa, dv, fragments):
                with self.subTest(runner=runner.__name__):
                    args = ["runner", str(tribe)]
                    if runner is not fragments:
                        args.append(str(root / "checkout"))
                    args.append(str(root / "work"))
                    output = io.StringIO()
                    env = {"RISCV_HOME": str(root), "RISCV_PREFIX": "riscv32-unknown-elf-",
                           "TRIBE_RISCV_DV_PYTHON": sys.executable}
                    with patch.dict(os.environ, env), patch.object(subprocess, "run", side_effect=run), \
                         patch.object(dv, "patch_riscv_dv_pygen"), contextlib.redirect_stdout(output):
                        self.assertEqual(runner.main(args), 1)
                    self.assertIn("Spike reference model cannot start", output.getvalue())
                    self.assertIn(".load_spike.sh", output.getvalue())
            self.assertEqual(len(calls), 3)

    def test_loader_rebuilds_broken_cached_spike_and_verifies_install(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            tests = root / "tribe_cpu" / "tests"
            scripts = tests / "scripts"
            scripts.mkdir(parents=True)
            original = pathlib.Path(__file__).resolve().parent
            shutil.copy(original.parent / ".load_spike.sh", tests)
            shutil.copy(original / "riscv_dependencies.py", scripts)
            build = tests / "riscv-isa-sim" / "build"
            build.mkdir(parents=True)
            (build / "Makefile").touch()
            self.executable(build, "spike", "exit 127")
            self.executable(build.parent, "configure", 'echo "configure $*" >> "$TRACE"')
            prefix = root / "install"
            (prefix / "bin").mkdir(parents=True)
            self.executable(prefix / "bin", "dtc", "exit 0")
            conda = root / "conda"
            (conda / "include" / "boost").mkdir(parents=True)
            (conda / "lib").mkdir()
            (conda / "lib" / "libboost_regex.so").touch()
            tools = root / "tools"
            tools.mkdir()
            # Stub downloads and dependency imports, but execute the real runtime check.
            python = tools / "python"
            python.write_text(f"#!{sys.executable}\n" +
                              "import os, sys\n"
                              "if sys.argv[1].endswith('riscv_dependencies.py'):\n"
                              "    os.execv(sys.executable, [sys.executable, *sys.argv[1:]])\n")
            python.chmod(0o755)
            self.executable(tools, "make", '\n'.join([
                'echo "make $*" >> "$TRACE"',
                'case "$1" in',
                'clean) rm -f spike ;;',
                'install) cp spike "$SPIKE_PREFIX/bin/spike" ;;',
                '*) printf "#!/bin/sh\\nexit 0\\n" > spike; chmod +x spike ;;',
                'esac',
            ]))
            env = os.environ.copy()
            env.update(PATH=str(tools) + os.pathsep + env.get("PATH", ""),
                       CONDA_PREFIX=str(conda), PYTHON_BIN=str(python),
                       SPIKE_PREFIX=str(prefix), RISCV_HOME=str(prefix),
                       TRIBE_PYDEPS_DIR=str(root / "pydeps"), TRACE=str(root / "trace"))
            command = ["bash", str(tests / ".load_spike.sh")]
            result = subprocess.run(command, env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            trace = (root / "trace").read_text()
            self.assertLess(trace.index("make clean"), trace.index("configure"))
            self.assertIn("--with-boost=" + str(conda), trace)
            self.assertIn("make install", trace)
            (root / "trace").write_text("")
            result = subprocess.run(command, env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn("make clean", (root / "trace").read_text())
            self.executable(tools, "make", 'if [ "$1" = install ]; then\n'
                            '  printf "#!/bin/sh\\nexit 127\\n" > "$SPIKE_PREFIX/bin/spike"\n'
                            'fi')
            result = subprocess.run(command, env=env, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Spike reference model cannot start", result.stderr)

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
