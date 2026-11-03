#!/usr/bin/env python3
"""Measure receiver clock-root loading and the remaining external-wire budget.

This accounts for extracted wire and characterized library pin capacitance.
The result is an integration budget, not a pulse-integrity or timing pass.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

from receive_interface_loads import extract


def digest(path):
    # Receiver extraction can exceed hundreds of MiB. Hash it in bounded
    # chunks while the full-route continuation may be using most host memory.
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def input_spef(path, port='clk'):
    """Keep the name map and one input net without loading a full receiver SPEF."""
    retained, selected = [], set()
    header, keep = True, False
    with Path(path).open() as source:
        for line in source:
            if line.startswith('*D_NET '):
                header = False
                keep = line.split()[1] in selected
            if header:
                retained.append(line)
                name = re.fullmatch(r'(\*\d+) (\S+)\s*', line)
                if name and name[2] == port:
                    selected.add(name[1])
            elif keep:
                retained.append(line)
    if len(selected) != 1:
        raise ValueError('missing or ambiguous receiver input in SPEF name map: ' + port)
    return ''.join(retained)


def budget(record, limit_ff):
    if not math.isfinite(limit_ff) or limit_ff <= 0:
        raise ValueError('input load budget must be finite and positive')
    worst = max(c['opposite_equal_swing_characterized_total_pf'] for c in record['corners'].values()) * 1000
    return dict(receiver_root_maximum_ff=worst, load_budget_ff=limit_ff,
                remaining_external_wire_ff=limit_ff - worst,
                fits_before_external_wiring=worst <= limit_ff)


def run(args):
    route = args.route.resolve()
    wait = getattr(args, 'wait_for_probe_seconds', 0)
    if not 0 <= wait <= 18000:
        raise ValueError('probe wait must be between zero and 18000 seconds')
    scripts = [Path(__file__).resolve(), Path(__file__).with_name('receive_interface_loads.py')]
    if wait:
        from receiver_layout import wait_for_producer
        scripts.append(Path(__file__).with_name('receiver_layout.py'))
    script_hashes = {str(p): digest(p) for p in scripts}
    if wait:
        wait_for_producer(route, 'early_route_diagnostic_complete', wait)
    if any(digest(p) != expected for p, expected in script_hashes.items()):
        raise ValueError('receiver measurement source changed while waiting')
    receipt = route / 'checks.json'
    proof = json.loads(receipt.read_text())
    if (proof.get('stage') != 'complete' or proof.get('status') not in
            ('early_route_diagnostic_complete', 'routed_timing_candidate_pass', 'routed_timing_fail')):
        raise ValueError('completed routed receiver measurement required')
    endpoints = proof.get('connectivity', proof.get('routed_connectivity', {}))
    if endpoints.get('status') != 'pass' or endpoints.get('top') != 'PCSReceiver':
        raise ValueError('receiver endpoint proof required')
    spef = route / 'core.spef'
    if proof['artifact_sha256'].get(str(spef)) != digest(spef):
        raise ValueError('changed receiver SPEF')
    sources = [receipt, spef, *scripts]
    libraries = {}
    for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        path = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib' / f'sg13g2_stdcell_{corner}.lib'
        if proof['source_sha256'].get(str(path)) != digest(path):
            raise ValueError('changed receiver clock library')
        sources.append(path)
        libraries[corner] = path.read_text()
    hashes = {str(p): digest(p) for p in sources}
    selected = input_spef(spef)
    if re.findall(r'^\*DESIGN "([^"]+)"$', selected, re.M) != ['PCSReceiver']:
        raise ValueError('SPEF is not the receiver')
    record = extract(selected, libraries, ports=['clk'])[0]
    report = dict(status='clock_load_report', route_status=proof['status'],
        source_sha256=hashes, load=record, budget=budget(record, args.budget_ff),
        clock_interface_qualified=False, full_chip_qualified=False, scope=__doc__)
    reset_limit = getattr(args, 'reset_budget_ff', None)
    if reset_limit is not None:
        reset = extract(input_spef(spef, 'reset'), libraries, ports=['reset'])[0]
        report['reset_load'] = reset
        report['reset_budget'] = budget(reset, reset_limit)
        report['reset_interface_qualified'] = False
    if any(digest(p) != expected for p, expected in hashes.items()):
        raise ValueError('receiver clock measurement input changed')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as target:
        target.write(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('budget', 'reset_budget') if key in report}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('route', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=Path(__file__).resolve().parents[2] / 'pdk/IHPSG13G2')
    parser.add_argument('--budget-ff', type=float, default=55,
                        help='total driver load bound; extracted clock gate has passed at 55 fF')
    parser.add_argument('--reset-budget-ff', type=float,
                        help='also measure reset against this total load bound (controller tested at 80 fF)')
    parser.add_argument('--wait-for-probe-seconds', type=int, default=0,
                        help='wait for an active early-route probe without taking the EDA memory slot')
    run(parser.parse_args())
