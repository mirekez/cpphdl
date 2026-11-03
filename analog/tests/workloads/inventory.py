"""Read CTest registrations without executing tests; inventory reference workloads."""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import subprocess


def option(command, flag, cmake=None):
    if flag in command:
        return command[command.index(flag) + 1]
    if cmake:
        return next((value.split('=', 1)[1] for value in command if value.startswith(cmake + '=')), '')
    return ''


def classify(test):
    name, command = test['name'], test.get('command', [])
    if not any('ngspice' in arg.lower() for arg in command):
        return None
    flow = option(command, '--flow', '-DFLOW')
    if flow in ('cpp', 'compare') or '--emit-only' in command:
        return None
    if any(arg.endswith('/sim_adc_controller_io.py') for arg in command):
        return None  # Uses ngspice headers for an ABI test, not its solver.
    tags = []
    groups = {
        'dc_bias': ('mos_dc', 'current_dc', 'current_cell', 'pullup', 'bias', 'reference', 'divider', 'mos_regions'),
        'charge_storage': ('storage', 'sampler', 'track_hold', 'charge', '.rc', 'floating'),
        'switching': ('inverter', 'latch', 'switch', 'level_', 'driver', 'translator', 'flp'),
        'analog_feedback': ('opamp', 'amplifier', 'ldo', 'agc', 'gain', 'frontend', 'preamplifier'),
        'clock_feedback': ('clock', 'oscillator', 'pll', 'cdr', 'phase_frequency'),
        'mixed_adc': ('conversion', 'adc_', 'comparator', 'decision', 'dac'),
        'transmit_pam': ('transmit', 'driver', 'emphasis', 'pam5'),
        'receive_channel': ('link', 'channel', 'ctle', 'dfe', 'mdi', 'slicer', 'equaliz'),
        'noise_jitter': ('noise', 'noisy', 'jitter'),
        'process_corners': ('.psp', '.pvt', 'mos_tt', 'mos_ss', 'mos_ff', 'ss_hot', 'ff_cold', '.process'),
        'extracted_devices': ('extracted', 'physical', 'layout', 'resistor_body'),
        'initial_conditions': ('startup', 'initial', 'acquisition'),
        'simulation_infrastructure': ('time_grid', 'live_clock', 'hierarchy', 'primitives', 'fault', 'guards'),
    }
    for family, tokens in groups.items():
        if any(token in name for token in tokens):
            tags.append(family)
    if not tags:
        tags = ['review_required']
    process = '.ihp' in name or '.pdk.' in name
    return dict(test=name, domain='IHPSG13G2' if process else '10G' if '.10g.' in name else
                '1G' if '.1g.' in name else 'generic', flow=flow or 'combined_or_direct',
                workload_tags=';'.join(tags), process_equivalence='unproven' if process else 'not_applicable')


def inventory(builds):
    tests = {}
    for build in builds:
        result = subprocess.run(['ctest', '--test-dir', str(build), '--show-only=json-v1'],
                                check=True, capture_output=True, text=True, timeout=30)
        for test in json.loads(result.stdout)['tests']:
            row = classify(test)
            # Preserve an enabled reference registration when another build
            # has disabled it. Count distinct names, not duplicate build dirs.
            if row is not None:
                tests[test['name']] = row
    return [tests[name] for name in sorted(tests)]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = inventory(args.build)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=('test', 'domain', 'flow', 'workload_tags', 'process_equivalence'), delimiter='\t')
        writer.writeheader()
        writer.writerows(rows)
    print(f'{len(rows)} distinct reference test registrations across the supplied builds')
    print(dict(Counter(row['domain'] for row in rows)))
    print('Registrations are not completed runs or a measured count of slow tests.')
    print('Tags are discovery aids; review_required and process equivalence gaps need explicit review.')
