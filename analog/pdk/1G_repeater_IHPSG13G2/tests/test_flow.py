#!/usr/bin/env python3
"""Regression for the evidence gates and the incomplete-chip refusal."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("repeater_checks", SOURCE / "checks.py")
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)


class Evidence(unittest.TestCase):
    def test_drc_refuses_missing_malformed_and_violations(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "report.lyrdb"
            with self.assertRaises(OSError):
                checks.check_drc(path)
            path.write_text("<report-database><categories/><items/></report-database>")
            with self.assertRaises(ValueError):
                checks.check_drc(path)
            path.write_text("<report-database><categories><category/></categories><items>"
                            "<item><category>Metal1.a</category></item></items></report-database>")
            with self.assertRaises(ValueError):
                checks.check_drc(path)

    def test_timing_refuses_negative_and_nonfinite_slack(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            (work / "setup.rpt").write_text("worst slack max -0.01\n")
            with self.assertRaises(ValueError):
                checks.check_timing(work)
            (work / "setup.rpt").write_text("worst slack max nan\n")
            with self.assertRaises(ValueError):
                checks.check_timing(work)

    def test_empty_partial_rule_report_does_not_prove_full_drc(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "partial.lyrdb"
            path.write_text("<report-database><categories><category><name>Metal1.a</name></category>"
                            "</categories><items/></report-database>")
            with self.assertRaises(ValueError):
                checks.check_drc(path)
    def test_missing_extraction_is_not_timing_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            (work / "setup.rpt").write_text("worst slack max 0.5\n")
            (work / "hold.rpt").write_text("worst slack min 0.1\n")
            (work / "electrical.rpt").write_text("")
            (work / "core.spef").write_text("*SPEF 1481-1998\n")
            with self.assertRaises(ValueError):
                checks.check_timing(work)

    def test_complete_chip_target_cannot_publish_a_core(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            (work / "repeater_core.gds").write_bytes(b"stale success")
            result = subprocess.run([sys.executable, str(SOURCE / "flow.py"), "--target", "chip", "--work", str(work)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "flow.json").read_text())
            self.assertEqual(report["status"], "blocked")
            self.assertFalse(report["full_1000base_t_chip"])
            self.assertGreaterEqual(len(report["missing"]), 5)
            self.assertFalse((work / "repeater_core.gds").exists())


if __name__ == "__main__":
    unittest.main()
