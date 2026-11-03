#!/usr/bin/env python3
"""Check the physical clock output against the receiver's actual STA edge contract.

Recheck retained PVT traces or repeat the same extracted circuit at an explicit
load. A passing load experiment still requires the receiver and interconnect
load to fit that budget; it does not qualify a connected clock tree.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(ROOT / 'VerilogA'), str(HERE), str(HERE / 'tests'),
                str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from compact_trace import read_spice
from input_charge import digest, verify_files
from processing_clock_gate import crossings
from processing_clock_gate_pex import HEADER, qualify
from tool_process import configure_cleanup, execute

CONDITIONS = {'mos_tt': (1.2, 'typ_1p20V_25C'),
              'mos_ss': (1.08, 'slow_1p08V_125C'),
              'mos_ff': (1.32, 'fast_1p32V_m40C')}


def thresholds(library):
    result = {}
    for edge in ('rise', 'fall'):
        values = []
        for bound in ('lower', 'upper'):
            found = re.findall(r'\bslew_' + bound + '_threshold_pct_' + edge + r'\s*:\s*([^;]+);', library)
            if len(found) != 1:
                raise ValueError('missing or ambiguous Liberty slew threshold')
            values.append(float(found[0]) / 100)
        if not 0 < values[0] < values[1] < 1:
            raise ValueError('invalid Liberty slew thresholds')
        result[edge] = values
    if re.findall(r'\bslew_derate_from_library\s*:\s*([^;]+);', library) != ['1']:
        raise ValueError('unsupported Liberty slew derating')
    return result


def clock_limit(sdc):
    values = re.findall(r'^set_input_transition\s+(\S+)\s+\[get_ports \{clk\}\]\s*$', sdc, re.M)
    if len(values) != 1 or not math.isfinite(float(values[0])) or float(values[0]) <= 0:
        raise ValueError('one finite positive receiver clk transition constraint required')
    return float(values[0]) * 1e-9


def measure(rows, supply, levels, limit):
    if not math.isfinite(limit) or limit <= 0:
        raise ValueError('invalid clock slew limit')
    pulses = qualify(rows, supply)
    measured = {}
    for edge, rising in (('rise', True), ('fall', False)):
        low, high = levels[edge]
        times = [crossings(rows, 3, supply * level, rising) for level in (low, .5, high)]
        if any(len(t) != pulses['pulses'] for t in times):
            raise ValueError('incomplete or repeated clock slew crossing')
        durations = []
        for a, middle, b in zip(*times):
            start, stop = (a, b) if rising else (b, a)
            if not start < middle < stop:
                raise ValueError('clock slew crossings are out of order')
            durations.append(stop - start)
        measured[edge + '_maximum_s'] = max(durations)
    return dict(**measured, limit_s=limit, pulse_metrics=pulses,
                passes=all(v <= limit for v in measured.values()))


def loaded_deck(text, reference_load, load_ff):
    if not math.isfinite(load_ff) or not 0 < load_ff <= 200:
        raise ValueError('explicit clock load must be in (0, 200] fF')
    matches = re.findall(r'^Cload OUT 0 ([0-9.eE+-]+)f IC=0$', text, re.M)
    if len(matches) != 1 or not math.isclose(float(matches[0]) * 1e-15, reference_load,
                                           rel_tol=1e-12, abs_tol=0):
        raise ValueError('reference clock load differs from its receipt')
    return re.sub(r'^Cload OUT 0 [0-9.eE+-]+f IC=0$',
                  f'Cload OUT 0 {load_ff:.17g}f IC=0', text, flags=re.M)


def run(args):
    reference = args.reference.resolve()
    proof = json.loads(reference.read_text())
    if proof.get('status') != 'pass' or set(proof.get('cases', {})) != set(CONDITIONS):
        raise ValueError('completed extracted clock-gate PVT reference required')
    hashes = dict(proof['source_sha256'])
    verify_files(hashes)
    files = [reference, args.constraints.resolve(), Path(__file__).resolve(),
             ROOT / 'VerilogA/compact_trace.py', HERE / 'tests/processing_clock_gate_pex.py',
             HERE / 'tests/processing_clock_gate.py', HERE / 'input_charge.py']
    libraries = {}
    for case, (_, suffix) in CONDITIONS.items():
        path = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib' / f'sg13g2_stdcell_{suffix}.lib'
        libraries[case] = thresholds(path.read_text())
        files.append(path)
    origin = Path(proof['work'])
    for case, recorded in proof['cases'].items():
        for name, key in (('bench.cir', 'deck_sha256'), ('spice.dat', 'trace_sha256')):
            path = origin / case / name
            if digest(path) != recorded[key]:
                raise ValueError('changed retained clock reference: ' + str(path))
            files.append(path)
    hashes.update({str(p): digest(p) for p in files})
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='recheck' if args.load_ff is None else 'waiting_for_simulation',
                  source_sha256=hashes, cases={}, full_chip_qualified=False,
                  output_load_F=proof['output_load_F'] if args.load_ff is None else args.load_ff * 1e-15,
                  scope=__doc__)

    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')

    save()
    try:
        limit = clock_limit(args.constraints.read_text())
        for case, (supply, _) in CONDITIONS.items():
            trace = origin / case / 'spice.dat'
            if args.load_ff is not None:
                destination = work / case
                destination.mkdir()
                circuit = loaded_deck((origin / case / 'bench.cir').read_text(),
                                      proof['output_load_F'], args.load_ff)
                (destination / 'bench.cir').write_text(circuit)
                ngspice = args.ngspice.resolve()
                if hashes.get(str(ngspice)) != digest(ngspice):
                    raise ValueError('simulator differs from retained clock reference')
                models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
                environment = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                                   PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models))
                report['stage'] = case
                save()
                with (destination / 'ngspice.log').open('w') as log:
                    execute(['prlimit', '--as=1073741824', '--core=0', '--', ngspice, '-b', 'bench.cir'],
                            cwd=destination, env=environment, stdout=log, stderr=-2,
                            timeout=2400, check=True, memory_exclusive=True)
                trace = destination / 'spice.dat'
            report['cases'][case] = measure(read_spice(trace, HEADER), supply, libraries[case], limit)
            report['cases'][case]['trace_sha256'] = digest(trace)
            save()
        verify_files(hashes)
        report.update(status='slew_screen_pass' if all(v['passes'] for v in report['cases'].values())
                      else 'input_slew_fail', stage='complete')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.rglob('*')
                                    if p.is_file() and p.name != 'checks.json'}
        save()
    print(report['status'], json.dumps({k: {n: v[n] for n in ('rise_maximum_s', 'fall_maximum_s')}
                                       for k, v in report['cases'].items()}))
    return report['status'] == 'slew_screen_pass'


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--load-ff', type=float, help='repeat the extracted PVT circuits at this explicit load')
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--ngspice', type=Path, default=ROOT / 'out/veriloga-toolchain/install/bin/ngspice')
    sys.exit(0 if run(parser.parse_args()) else 1)
