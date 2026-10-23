#!/usr/bin/env python3
"""A core, absent layout or stale report directory must not qualify as a chip."""
from pathlib import Path
from contextlib import redirect_stdout
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]


class PhysicalVerification(unittest.TestCase):
    def test_zero_exit_and_malformed_reports_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            (work / "pad.gds").write_bytes(b"negative fixture")
            (work / "pad.cdl").write_text(".SUBCKT RepeaterPadframe vdd\n.ENDS\n")
            (work / "interface.json").write_text('["vdd"]')
            environment = work / "pdk/.cpphdl/venv/bin"
            environment.mkdir(parents=True)
            os.symlink(sys.executable, environment / "python")
            (work / "pdk/.cpphdl/revision.txt").write_text("negative tool fixture\n")
            for kind in ("drc", "lvs"):
                directory = work / "pdk/ihp-sg13g2/libs.tech/klayout/tech" / kind
                directory.mkdir(parents=True)
                (directory / ("run_" + kind + ".py")).write_text(
                    "import sys\nfrom pathlib import Path\n"
                    "work = Path(sys.argv[sys.argv.index('--run_dir') + 1])\n"
                    "work.mkdir(parents=True)\n"
                    "(work / 'pad_RepeaterPadframe_full.lyrdb').write_text('<broken>')\n")
            sys.path.insert(0, str(SOURCE))
            spec = importlib.util.spec_from_file_location('physical_verification_negative', SOURCE / 'verify_physical.py')
            verification = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(verification)
            args = SimpleNamespace(layout=work / 'pad.gds', netlist=work / 'pad.cdl',
                interface=work / 'interface.json', top='RepeaterPadframe', pdk=work / 'pdk',
                tools=work / 'unused-tools', work=work / 'checks', mode='deep', timeout=5)
            # These runners only write malformed fixture reports. Keep them
            # outside the real EDA memory queue while testing the production
            # report validation and its zero-exit rejection.
            with patch.object(verification, 'execute', side_effect=subprocess.run), redirect_stdout(io.StringIO()):
                result = verification.verify(args)
            self.assertEqual(result, 1)
            report = json.loads((work / "checks/physical_checks.json").read_text())
            self.assertEqual(report["status"], "failed")
            self.assertEqual(report["checks"], {})
            self.assertEqual(len(report["errors"]), 2)
            self.assertTrue((work / "checks/lvs").is_dir(), "LVS did not run after DRC failed")

    def test_missing_inputs_fail_and_write_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            interface = work / "interface.json"
            interface.write_text('["vdd"]')
            result = subprocess.run([sys.executable, str(SOURCE / "verify_physical.py"),
                "--layout", str(work / "missing.gds"), "--netlist", str(work / "missing.cdl"),
                "--interface", str(interface), "--top", "RepeaterChip", "--work", str(work / "checks")],
                text=True, capture_output=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "checks/physical_checks.json").read_text())
            self.assertEqual(report["status"], "failed")
            self.assertFalse(report["full_1000base_t_chip"])
            self.assertEqual(report["checks"], {})

    def test_gmii_interface_is_not_copper_chip(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            (work / "core.gds").write_bytes(b"not a layout")
            (work / "core.cdl").write_text(".SUBCKT RepeaterChip rx tx\n.ENDS\n")
            (work / "interface.json").write_text('["rx", "tx"]')
            result = subprocess.run([sys.executable, str(SOURCE / "verify_physical.py"),
                "--layout", str(work / "core.gds"), "--netlist", str(work / "core.cdl"),
                "--interface", str(work / "interface.json"), "--top", "RepeaterChip", "--work", str(work / "checks")],
                text=True, capture_output=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "checks/physical_checks.json").read_text())
            self.assertIn("two four-pair MDIs", report["errors"][0])
            self.assertEqual(report["checks"], {})


if __name__ == "__main__":
    unittest.main()
