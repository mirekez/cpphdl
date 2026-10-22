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
    def test_zero_exit_partial_run_cannot_omit_a_rule_table(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rules = root / "rules/rule_decks/feol"
            rules.mkdir(parents=True)
            report = root / "part_full.lyrdb"
            for index in range(37):
                table = f"table{index}"
                (rules / f"5_{index}_{table}.drc").write_text("rule inventory fixture")
                if index != 17:
                    (root / f"part_{table}.log").write_text(
                        f"KLayout DRC run for tables '{table}' completed in 1 seconds\n")
            with self.assertRaisesRegex(ValueError, "table did not complete: table17"):
                checks.check_drc_completion(report, root / "rules")
            (root / "part_table17.log").write_text("KLayout DRC run for tables 'table17' completed in 1 seconds\n")
            (root / "part_sg13g2_maximal.log").write_text("Starting maximum ruleSet\n")
            with self.assertRaisesRegex(ValueError, "maximum DRC rules did not complete"):
                checks.check_drc_completion(report, root / "rules")
            (root / "part_sg13g2_maximal.log").write_text("KLayout DRC run for maximum ruleSet completed\n")
            (root / "part_density.log").write_text("KLayout DRC run for density table completed\n")
            self.assertEqual(checks.check_drc_completion(report, root / "rules")["main_tables"], 37)

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
