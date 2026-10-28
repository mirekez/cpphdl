#!/usr/bin/env python3
"""Run the original receiver contract on the distributed-current layout candidate."""
import argparse
import ast
from contextlib import contextmanager
import json
from pathlib import Path
import runpy
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'VerilogA')]
from extract_analog import digest
from reserved_trace import capture_reserved_trace
from validate_trace import validate_rows

# Keep one electrical contract and one complete host-composition implementation.
# Each invocation loads its own globals; other in-flight tests are unaffected.
checker = runpy.run_path(str(SOURCE.with_name('receive_common_extracted.py')))
shared = checker['run'].__globals__
original_evidence = shared['layout_evidence']
original_grid = shared['windowed_transient']


def enable_streaming_validation():
    # This contract discards checked_rows' normalized return value. Keep its
    # complete validation, but avoid duplicating the large saved trajectory.
    leaf = checker['contract'].__globals__['reference_contract'].__globals__['original_contract']
    tree = ast.parse(Path(leaf.__code__.co_filename).read_text())
    contract, = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'contract']
    first = contract.body[0]
    if (not isinstance(first, ast.Expr) or not isinstance(first.value, ast.Call) or
            not isinstance(first.value.func, ast.Name) or first.value.func.id != 'checked_rows'):
        raise ValueError('receiver validation no longer discards its normalized copy')
    leaf.__globals__['checked_rows'] = validate_rows


def evidence(path):
    proof, hashes = original_evidence(path)
    collectors = proof['geometry'].get('sink_upper_current', {}).get('collectors', {})
    if (set(collectors) != {'D', 'S'} or any(
            collectors[name].get('stripes') != stripes or
            collectors[name].get('via3_cuts_per_stripe') != 8 or
            collectors[name].get('via4_cuts_per_stripe') != 8
            for name, stripes in (('D', 15), ('S', 16)))):
        raise ValueError('eight-cut receive-current candidate geometry required')
    for source in (SOURCE, ANALOG / 'VerilogA/reserved_trace.py', ANALOG / 'VerilogA/validate_trace.py'):
        hashes[str(source)] = digest(source)
    return proof, hashes


def grid(deck, *args, **kwargs):
    deck, timing = original_grid(deck, *args, **kwargs)
    if deck.count('.control\n') != 1 or '.options klu' in deck or 'set num_threads=' in deck:
        raise ValueError('unexpected receiver solver controls')
    return deck.replace('.control\n', '.options klu\n.control\nset num_threads=1\n', 1), timing


@contextmanager
def capture(path):
    # Complete baseline archives exceed 64 MiB. Reserve physical disk blocks
    # before starting ngspice; preserve every emitted sample and its hash.
    with capture_reserved_trace(path, 256 * 1024 ** 2) as trace:
        yield trace
    path.with_name('trace-storage.json').write_text(json.dumps(trace.metadata, indent=2) + '\n')


if __name__ == '__main__':
    checker['configure_cleanup']()
    enable_streaming_validation()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout', 'bench', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--refined', action='store_true')
    parser.add_argument('--emit-only', action='store_true')
    shared.update(layout_evidence=evidence, windowed_transient=grid, capture_trace=capture)
    checker['run'](parser.parse_args())
