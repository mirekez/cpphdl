#!/usr/bin/env python3
"""Ten-MOS 1.2-to-3.3 V interface with LV/HV PSP devices and finite loads."""
import argparse
import gzip
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[2] / 'tests'))
from level_shifter import contracts
sys.path.insert(0, str(SOURCE.parents[1]))
from mos_geometry import size_mos_instances


def characterize(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    inputs = [SOURCE, SOURCE.parents[1] / 'mos_geometry.py', SOURCE.parents[3] / 'basic/level_shifter.hpp', SOURCE.parents[2] / 'tests/level_shifter.py']
    if args.cascoded:
        inputs.append(SOURCE.parents[3] / 'basic/cascoded_level_shifter.hpp')
    if args.clock_interface:
        inputs.append(SOURCE.parents[2] / 'clock_level_shifter.hpp')
    inputs.append(args.ngspice.resolve())
    inputs += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    provenance = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(models))
    experiments, failures = [], []
    half_period = .5e-6 / args.clock_mhz
    startup = 32e-9 if args.cascoded else 0.
    corners = ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs') if args.corner == 'all' else (args.corner,)
    voltages = ((1.08, 2.97), (1.2, 3.3), (1.32, 3.63), (1.08, 3.63), (1.32, 2.97))
    temperatures, loads = (-40, 27, 125), (False, True)
    if args.reject_direct:
        corners, temperatures, voltages, loads = ('mos_tt',), (-40,), ((1.08, 3.63),), (True,)
    for corner, temperature, (low, high), loaded in itertools.product(
            corners, temperatures, voltages, loads):
        case = work / f'{corner}_{temperature}_{low}_{high}_{int(loaded)}'
        case.mkdir(exist_ok=True)
        trace = case / 'trace.dat'
        trace.unlink(missing_ok=True)
        wave, previous = [(0, 0)], 0
        for symbol in range(1, 16):
            value = low if symbol & 1 else 0
            wave += [(startup + symbol * half_period, previous), (startup + symbol * half_period + .1e-9, value)]
            previous = value
        pwl = 'PWL(' + ' '.join(f'{t:.14g} {v:.14g}' for t, v in wave) + ')'
        # No ideal high-domain gate driver: LV inverter and cross-coupled HV
        # devices create the outputs. Four-terminal bodies use their own rails.
        deck = f'''SG13G2 mixed-voltage level shifter
.lib "{models / 'models/cornerMOSlv.lib'}" {corner}
.lib "{models / 'models/cornerMOShv.lib'}" {corner}
.temp {temperature}
VSUPPLY source 0 {high}
VLOGIC logic_source 0 {low}
RSUPPLY source vdd 10
RLOGIC logic_source vl 10
CSUPPLY vdd 0 2p
CLOGIC vl 0 .5p
VCMD command 0 {pwl}
RINPUT command a 50
XLI_N complement a 0 0 sg13_lv_nmos w=10u l=.13u
XLI_P complement a vl vl sg13_lv_pmos w=20u l=.13u
XPU translated translated_n vdd vdd sg13_hv_pmos w=5u l=.45u
XPU_N translated_n translated vdd vdd sg13_hv_pmos w=5u l=.45u
XPD translated complement 0 0 sg13_hv_nmos w=50u l=.45u
XPD_N translated_n a 0 0 sg13_hv_nmos w=50u l=.45u
XBUF_N y translated_n 0 0 sg13_hv_nmos w=10u l=.45u
XBUF_P y translated_n vdd vdd sg13_hv_pmos w=20u l=.45u
XBUFN_N yn translated 0 0 sg13_hv_nmos w=10u l=.45u
XBUFN_P yn translated vdd vdd sg13_hv_pmos w=20u l=.45u
CC complement 0 .01p
CT translated 0 .05p
CTN translated_n 0 .05p
CY y 0 .05p
CYN yn 0 .05p
CGD_N a translated_n .002p
CGD complement translated .002p
''' + ('CLOAD y 0 .2p\nRLOAD y 0 10000\n' if loaded else '') + f'''.options method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8
.control
set noaskquit
set wr_singlescale
set wr_vecnames
set numdgt=17
tran {min(7.8125e-12, half_period / 512):.17g} {startup + 16 * half_period:.17g}
wrdata trace.dat v(y) v(yn) v(a) v(vdd) v(vl) v(command)
quit
.endc
.end
'''
        if args.cascoded:
            changes = {
                'XPD translated complement 0 0 sg13_hv_nmos w=50u l=.45u':
                    'XPD translated bias low_node 0 sg13_hv_nmos w=80u l=.45u\n'
                    'XLOW low_node complement 0 0 sg13_lv_nmos w=40u l=.13u',
                'XPD_N translated_n a 0 0 sg13_hv_nmos w=50u l=.45u':
                    'XPD_N translated_n bias low_node_n 0 sg13_hv_nmos w=80u l=.45u\n'
                    'XLOW_N low_node_n a 0 0 sg13_lv_nmos w=40u l=.13u',
                'XPU translated translated_n vdd vdd sg13_hv_pmos w=5u l=.45u':
                    'XPU translated translated_n vdd vdd sg13_hv_pmos w=20u l=.45u',
                'XPU_N translated_n translated vdd vdd sg13_hv_pmos w=5u l=.45u':
                    'XPU_N translated_n translated vdd vdd sg13_hv_pmos w=20u l=.45u',
                'XBUF_N y translated_n 0 0 sg13_hv_nmos w=10u l=.45u':
                    'XBUF_N y translated_n 0 0 sg13_hv_nmos w=20u l=.45u',
                'XBUF_P y translated_n vdd vdd sg13_hv_pmos w=20u l=.45u':
                    'XBUF_P y translated_n vdd vdd sg13_hv_pmos w=40u l=.45u',
                'XBUFN_N yn translated 0 0 sg13_hv_nmos w=10u l=.45u':
                    'XBUFN_N yn translated 0 0 sg13_hv_nmos w=20u l=.45u',
                'XBUFN_P yn translated vdd vdd sg13_hv_pmos w=20u l=.45u':
                    'XBUFN_P yn translated vdd vdd sg13_hv_pmos w=40u l=.45u',
                'CT translated 0 .05p': 'CT translated 0 .01p',
                'CTN translated_n 0 .05p': 'CTN translated_n 0 .01p',
                'CGD_N a translated_n .002p': 'CGD_N a low_node_n .002p',
                'CGD complement translated .002p': 'CGD complement low_node .002p',
                '.options method=gear': 'RBIAS_TOP vl bias_ref 200\nRBIAS_BOTTOM bias_ref 0 800\n'
                    'RBIAS_FEED vdd bias 10000\nXBIAS bias bias bias_ref 0 sg13_hv_nmos w=100u l=.45u\n'
                    'CBIAS bias 0 1p\nCLOW low_node 0 .01p\nCLOW_N low_node_n 0 .01p\n.options method=gear',
                'v(vl) v(command)': 'v(vl) v(command) v(low_node) v(low_node_n) v(complement) v(bias)',
                f'{startup + 16 * half_period:.17g}\nwrdata': f'{startup + 16 * half_period:.17g} 0 '
                    f'{min(7.8125e-12, half_period / 512):.17g} uic\nwrdata',
            }
            for old, new in changes.items():
                if deck.count(old) != 1:
                    raise ValueError('review changed process topology before applying cascode mapping: ' + old)
                deck = deck.replace(old, new)
        if args.clock_interface:
            for old, new in (('CY y 0 .05p', 'CY y 0 .25p'), ('CYN yn 0 .05p', 'CYN yn 0 .25p')):
                if deck.count(old) != 1:
                    raise ValueError('review changed output storage before applying clock correction')
                deck = deck.replace(old, new)
        deck, geometry = size_mos_instances(deck)
        (case / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        (case / 'bench.cir').write_text(deck)
        try:
            process = subprocess.run([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case,
                                     env=environment, capture_output=True, text=True, timeout=args.case_timeout)
        except subprocess.TimeoutExpired as error:
            def text(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            (case / 'ngspice.log').write_text(text(error.stdout) + text(error.stderr) + '\nTIMEOUT\n')
            report_path.write_text(json.dumps(dict(status='timeout', case=case.name,
                timeout_s=args.case_timeout, experiments=experiments, failures=failures,
                source_sha256=provenance), indent=2) + '\n')
            raise
        log = process.stdout + process.stderr
        (case / 'ngspice.log').write_text(log)
        if process.returncode or re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small', log, re.I):
            raise ValueError('PSP level-shifter simulation failed: ' + str(case / 'ngspice.log'))
        with trace.open() as file:
            next(file)
            rows = [[float(value) for value in line.split()] for line in file if line.strip()]
        observed = [[row[0] - startup, *row[1:7]] for row in rows if row[0] >= startup]
        result = contracts(observed, low, high, loaded, half_period)
        if args.cascoded:
            exposure = max(max(abs(r[3]), abs(r[9]), abs(r[3] - r[9]), abs(r[3] - r[5]),
                               abs(r[3] - r[8]), abs(r[9] - r[7])) for r in rows)
            result['checks']['low_voltage_gate_exposure'] = exposure <= 1.65
            result['maximum_low_voltage_gate_difference_V'] = exposure
        result.pop('comparison_probes_V')
        result.pop('output_crossings')
        compressed = trace.with_suffix('.dat.gz')
        with trace.open('rb') as original, gzip.open(compressed, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(original, output)
        trace.unlink()
        result.update(corner=corner, temperature_C=temperature, logic_supply_V=low, supply_V=high, loaded=loaded,
                      artifact_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                       for p in (case / 'bench.cir', case / 'ngspice.log', compressed)})
        experiments.append(result)
        failures += [case.name + ': ' + name for name, passed in result['checks'].items() if not passed]
        report_path.write_text(json.dumps(dict(status='running', experiments=experiments,
            failures=failures, source_sha256=provenance), indent=2) + '\n')
        print(case.name + ': ' + ('pass' if all(result['checks'].values()) else 'fail'), flush=True)
    if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest for path, digest in provenance.items()):
        raise ValueError('level-shifter source/model changed during characterization')
    expected_rejection = False
    if args.reject_direct:
        failed_checks = {name for name, passed in experiments[0]['checks'].items() if not passed}
        expected_rejection = bool(failed_checks) and failed_checks <= {'output_low', 'output_high'}
    report_path.write_text(json.dumps(dict(status='pass' if expected_rejection else 'fail' if failures or args.reject_direct else 'pass',
        expected_rejection=expected_rejection, cases=len(experiments), clock_MHz=args.clock_mhz,
        topology='clock cascode with 200 fF output correction' if args.clock_interface else
                 'replica-biased cascode' if args.cascoded else 'direct HV pull-down', startup_s=startup,
        scope='PSP level-shifter process, supply and temperature; no mismatch or extracted layout qualification',
        same_equations_as_square_law=False, experiments=experiments, failures=failures,
        source_sha256=provenance), indent=2) + '\n')
    if args.reject_direct and not expected_rejection:
        raise ValueError('direct-HV negative control must complete and fail only its rail-settling checks')
    if failures and not expected_rejection:
        raise ValueError('\n'.join(failures))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--corner', choices=('all', 'mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), default='all')
    parser.add_argument('--clock-mhz', type=float, choices=(62.5, 250), default=62.5)
    parser.add_argument('--cascoded', action='store_true')
    parser.add_argument('--clock-interface', action='store_true')
    parser.add_argument('--reject-direct', action='store_true')
    parser.add_argument('--case-timeout', type=float, default=180)
    args = parser.parse_args()
    if args.clock_interface and (not args.cascoded or args.clock_mhz != 250):
        parser.error('--clock-interface requires --cascoded --clock-mhz 250')
    if args.reject_direct and (args.cascoded or args.clock_mhz != 250):
        parser.error('--reject-direct requires the direct topology at 250 MHz')
    if not 0 < args.case_timeout <= 1200:
        parser.error('--case-timeout must be positive and at most 1200 seconds')
    try:
        characterize(args)
    except BaseException as error:
        path = args.work.resolve() / 'checks.json'
        if path.is_file():
            result = json.loads(path.read_text())
            if result.get('status') == 'running':
                result.update(status='error', error=str(error), error_type=type(error).__name__)
                path.write_text(json.dumps(result, indent=2) + '\n')
        raise
