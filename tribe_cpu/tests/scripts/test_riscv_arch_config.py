#!/usr/bin/env python3
"""Regression tests for Tribe's generated riscv-arch-test configuration."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

import run_riscv_arch_test as runner


class RiscvArchConfigTest(unittest.TestCase):
    def test_udb_profile_removes_floating_point_support(self) -> None:
        source_text = """\
implemented_extensions:
  - { name: I, version: "= 2.1" }
  - { name: F, version: "= 2.2.0" }
  - { name: D, version: "= 2.2.0" }
  - { name: Zcf, version: "= 1.0.0" }
  - { name: Zcd, version: "= 1.0.0" }
  - { name: Zca, version: "= 1.0.0" }
params:
  MUTABLE_MISA_M: false
  # F params
  MUTABLE_MISA_F: false
  HW_MSTATUS_FS_DIRTY_UPDATE: precise
  MSTATUS_FS_LEGAL_VALUES: [0, 1, 2, 3]
  # D params
  MUTABLE_MISA_D: false
  MUTABLE_MISA_C: false
"""
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = pathlib.Path(temp_dir)
            source = temp / "source.yaml"
            destination = temp / "tribe.yaml"
            source.write_text(source_text, encoding="utf-8")

            runner.write_tribe_udb_config(source, destination)
            generated = destination.read_text(encoding="utf-8")

        for extension in ("F", "D", "Zcf", "Zcd"):
            self.assertNotIn(f"name: {extension},", generated)
        for parameter in (
            "MUTABLE_MISA_F",
            "HW_MSTATUS_FS_DIRTY_UPDATE",
            "MSTATUS_FS_LEGAL_VALUES",
            "MUTABLE_MISA_D",
        ):
            self.assertNotIn(parameter, generated)
        self.assertIn("name: I,", generated)
        self.assertIn("name: Zca,", generated)
        self.assertIn("MUTABLE_MISA_M: false", generated)
        self.assertIn("MUTABLE_MISA_C: false", generated)

    def test_rvtest_header_describes_tribe_rv32(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            output = pathlib.Path(temp_dir) / "rvtest_config.h"
            runner.write_tribe_rvtest_config(output)
            generated = output.read_text(encoding="utf-8")

        self.assertIn("#define UDB_MXLEN 32", generated)
        self.assertIn("#define RVMODEL_NUM_PMPS 0", generated)
        self.assertIn("#define ZAAMO_SUPPORTED", generated)
        self.assertIn("#define ZALRSC_SUPPORTED", generated)
        self.assertIn("#define ZCA_SUPPORTED", generated)


if __name__ == "__main__":
    unittest.main()
