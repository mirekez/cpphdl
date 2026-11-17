#!/usr/bin/env python3
"""Acceptance-gate regressions independent of archived campaign artifacts."""
import copy
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qualification import (LIMITS, REQUIRED_CHARACTERIZATION, evaluate, from_campaign, linearity_evidence,
                           power_evidence, require_current_source)


def campaign():
    direction = dict(complete_codes=[10, 11], missing_complete_codes=[],
                     dnl_bounds_lsb=[[-.2, -.1], [.1, .25]],
                     inl_bounds_lsb=[[-.12, -.08], [-.05, .04], [.1, .15]])
    power_case = dict(required=True, failed_operating_points=0, conversions=2,
                      configuration=dict(sample_period_s=50e-9),
                      full_cycle_io_energy_J=[400e-12, 450e-12],
                      full_cycle_core_energy_J=[50e-12, 50e-12],
                      full_cycle_io_peak_A=[.019, .020],
                      full_cycle_core_peak_A=[.001, .001])
    spectrum = dict(sndr_db=40, enob_at_stimulus=6.35, sfdr_db=48)
    characterization = {name: dict(status="PASS", suite=name, cases=[copy.deepcopy(power_case)])
                        for name in REQUIRED_CHARACTERIZATION}
    characterization["frequency"]["cases"][0]["spectrum"] = spectrum
    characterization["power"]["power_refinement"] = dict(
        method="BDF2", steps_ps=[.3, .15],
        relative_differences={"full_cycle_io_energy_J":.001, "full_cycle_io_peak_A":.01},
        energy_charge_limit=.01, peak_limit=.05)
    return dict(status="PASS", transitions=dict(status="PASS", smoke=False, up=direction,
                                 down=copy.deepcopy(direction)),
                characterization=characterization,
                conversions=[dict(experiment=name, absolute_hold_V="0.005", **{"pass": "1"})
                             for name in ("conversion", "range")])


class QualificationTest(unittest.TestCase):
    def test_measured_evidence_stays_unqualified_without_product_limits(self):
        result = from_campaign(campaign(), {})
        self.assertEqual(result["status"], "INCOMPLETE")
        self.assertEqual({x["status"] for x in result["checks"].values()}, {"UNSPECIFIED"})
        self.assertEqual(result["measurements"]["maximum_abs_dnl_lsb"], .25)
        self.assertAlmostEqual(result["measurements"]["maximum_full_cycle_rail_power_W"], .010)
        self.assertAlmostEqual(result["measurements"]["maximum_peak_rail_current_A"], .020)

    def test_limits_pass_and_fail_at_boundary(self):
        evidence = from_campaign(campaign(), {})["measurements"]
        limits = {name: evidence[key] for name, (key, _) in LIMITS.items()}
        self.assertEqual(evaluate(evidence, limits)["status"], "PASS")
        limits["dnl"] = .24
        limits["average_power"] = .009
        result = evaluate(evidence, limits)
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["checks"]["dnl"]["status"], "FAIL")
        self.assertEqual(result["checks"]["average_power"]["status"], "FAIL")

    def test_partial_limit_is_not_full_qualification(self):
        result = from_campaign(campaign(), {"dnl": .3})
        self.assertEqual(result["status"], "INCOMPLETE")
        self.assertEqual(result["checks"]["dnl"]["status"], "PASS")
        with self.assertRaises(ValueError):
            from_campaign(campaign(), {"bogus": 1})

    def test_rejects_incomplete_linearity(self):
        for mutate in (lambda c: c["transitions"].update(smoke=True),
                       lambda c: c["transitions"]["down"].update(missing_complete_codes=[10]),
                       lambda c: c["transitions"]["up"].update(complete_codes=[10, 12]),
                       lambda c: c["transitions"]["up"].update(dnl_bounds_lsb=[[0, 1]])):
            record = campaign()
            mutate(record)
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                linearity_evidence(record["transitions"])

    def test_rejects_incomplete_or_unrefined_power(self):
        for mutate in (lambda c: c["cases"][0].update(failed_operating_points=1),
                       lambda c: c["cases"][0].update(full_cycle_io_energy_J=[1e-12]),
                       lambda c: c["cases"][0].update(full_cycle_io_energy_J=[-1e-12, 1e-12]),
                       lambda c: c["power_refinement"].update(method="BE"),
                       lambda c: c["power_refinement"]["relative_differences"].update(full_cycle_io_peak_A=.06)):
            record = campaign()["characterization"]
            mutate(record["power"])
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                power_evidence(record)

    def test_power_includes_worst_required_stress_case(self):
        record = campaign()["characterization"]
        record["jitter"]["cases"][0]["full_cycle_io_peak_A"][1] = .136
        record["acquisition"]["cases"][0]["full_cycle_io_energy_J"][1] = 600e-12
        evidence = power_evidence(record)
        self.assertAlmostEqual(evidence["maximum_peak_rail_current_A"], .136)
        self.assertAlmostEqual(evidence["maximum_full_cycle_rail_power_W"], .013)
        self.assertEqual(evidence["required_cycles"], 2 * len(REQUIRED_CHARACTERIZATION))

    def test_rejects_failed_rated_hold_record(self):
        record = campaign()
        record["conversions"][1]["pass"] = "0"
        with self.assertRaises(ValueError):
            from_campaign(record, {})

    def test_rejects_failed_or_omitted_campaign(self):
        record = campaign()
        record["status"] = "FAIL"
        with self.assertRaises(ValueError):
            from_campaign(record, {})
        record["status"] = "PASS"
        record["omitted_suites"] = ["characterize_power"]
        with self.assertRaises(ValueError):
            from_campaign(record, {})

    def test_source_lock_checks_content_and_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            path = repo / "analog/lib/adc_sar/demo.py"
            path.parent.mkdir(parents=True)
            path.write_text("a\n")
            record = dict(source_sha256={"analog/lib/adc_sar/demo.py":
                                         hashlib.sha256(path.read_bytes()).hexdigest()})
            require_current_source(record, repo)
            path.write_text("b\n")
            with self.assertRaises(ValueError):
                require_current_source(record, repo)
            path.write_text("a\n")
            (path.parent / "new.py").write_text("x\n")
            with self.assertRaises(ValueError):
                require_current_source(record, repo)


if __name__ == "__main__":
    unittest.main()
