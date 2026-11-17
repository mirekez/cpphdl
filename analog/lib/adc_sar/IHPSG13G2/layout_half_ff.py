#!/usr/bin/env python3
"""Place a slice with legal half-femtofarad MIM feedback cells.

The ordinary one-femtofarad path is preserved. This adapter keeps the active
content-hashed layout.py unchanged while the new cell receives DRC/LVS tests.
"""

import argparse
import json
import math
from pathlib import Path

import layout
from half_ff_capacitor_layout import append_half_ff

original_one_ff = layout.append_one_ff


def small_mim(layout_db, pdk, name, value):
    if math.isclose(value, .5e-15, rel_tol=1e-12, abs_tol=0):
        return append_half_ff(layout_db, pdk, name, value)
    return original_one_ff(layout_db, pdk, name, value)


def build(circuit, pdk, work, order):
    layout.append_one_ff = small_mim
    try:
        return layout.build(json.loads(circuit.read_text()), pdk.resolve(),
                            work.resolve(), json.loads(order.read_text()))
    finally:
        layout.append_one_ff = original_one_ff


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("circuit", "pdk", "work", "order"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    build(args.circuit, args.pdk, args.work, args.order)
