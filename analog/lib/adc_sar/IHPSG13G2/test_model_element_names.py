#!/usr/bin/env python3
"""Verify the actual Analog++ element names against the placed image labels."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from element_names import check_model_export, names_for, source_hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exporter", type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    mapping = json.loads((here / "element_names.json").read_text())
    if mapping["model_source_sha256"] != source_hashes():
        raise ValueError("element-name map is stale relative to the C++ model")
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run([str(args.exporter.resolve()), directory], check=True, capture_output=True, text=True)
        check_model_export(mapping["elements"], Path(directory) / "circuit.json")
    for placement in here.rglob("adc_sar_slice.placement.json"):
        if names_for(json.loads(placement.read_text())) != mapping["elements"]:
            raise ValueError(f"model names differ from placement {placement}")
    print("PASS: 137 Analog++ model names match every placed image label")


if __name__ == "__main__":
    main()
