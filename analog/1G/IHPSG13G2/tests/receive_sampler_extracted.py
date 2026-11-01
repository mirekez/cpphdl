"""Check extracted sampler acquisition, hold and isolation against independent PSP circuits."""
import argparse
import gzip
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / 'VerilogA'),
               str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from receive_sampler_layout import PORTS, TOP
from named_node_extraction import extract_named
from extract_analog import digest
from input_charge import verify_files
from compact_trace import checked_rows
from reserved_trace import capture_reserved_trace
from tool_process import configure_cleanup, execute, memory_slot


def reference(geometry):
    lines = ['.subckt ReferenceSampler ' + ' '.join(PORTS)]
    for index, part in enumerate(geometry['components']):
        g, pins = part['geometry'], part['connections']
        if part['kind'] == 'mos':
            lines.append(f'Xmos{index} ' + ' '.join(pins[n] for n in ('D', 'G', 'S', 'B')) +
                f' {g["model"]} w={g["total_width_um"]}u l={g["length_um"]}u ng={g["fingers"]}')
        elif part['kind'] == 'capacitor':
            count, unit = g.get('capacitors', 1), g.get('unit', g)
            nodes = [pins['PLUS']] + [f'J{index}_{k}' for k in range(count - 1)] + [pins['MINUS']]
            for k in range(count):
                lines.append(f'Xcap{index}_{k} {nodes[k]} {nodes[k + 1]} cap_cmim '
                    f'w={unit["width_um"]}u l={unit["length_um"]}u ic=0')
        else:
            raise ValueError('unknown sampler reference component')
    return '\n'.join(lines + ['.ends ReferenceSampler', ''])


def experiment(physical, schematic, models, target, corner, temperature):
    text = ('* Cold sampler, finite source/supply/clock impedances, later input disturbance\n'
        f'.lib "{models / "models/cornerMOShv.lib"}" {corner}\n'
        f'.lib "{models / "models/cornerCAP.lib"}" cap_typ\n'
        f'.temp {temperature}\n' + physical + schematic)
    # Independent supply and stimulus branches keep the reference and DUT
    # from correcting each other's voltage errors through the testbench.
    for suffix, model in (('P', 'ExtractedAnalog'), ('R', 'ReferenceSampler')):
        disturbed = target + (.25 if target <= 1.6 else -.25)
        text += (f'VS{suffix} SP{suffix} 0 PWL(0 0 1n 0 10n 3.3)\n'
            f'RS{suffix} SP{suffix} VDD{suffix} 1\nCS{suffix} VDD{suffix} 0 2p ic=0\n'
            f'VI{suffix} SI{suffix} 0 PWL(0 0 10n 0 12n {target} 48n {target} 48.4n {disturbed})\n'
            f'RI{suffix} SI{suffix} IN{suffix} 50\n'
            f'VC{suffix} SC{suffix} 0 PWL(0 0 15n 0 15.4n 3.3 39.8n 3.3 40.2n 0)\n'
            f'RC{suffix} SC{suffix} CK{suffix} 50\n'
            f'Xsample{suffix} VDD{suffix} 0 IN{suffix} CK{suffix} OUT{suffix} {model}\n'
            f'CL{suffix} OUT{suffix} 0 100f ic=0\nRL{suffix} OUT{suffix} 0 1g\n')
    observations = 'v(OUTP) v(OUTR) v(INP) v(INR) v(CKP) v(CKR) v(VDDP) v(VDDR)'
    return text + ('.options klu method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8\n'
        '.save ' + observations + '\n.control\nset num_threads=1\nset wr_singlescale\n'
        'set wr_vecnames\nset numdgt=17\ntran 2p 60n 0 2p uic\n'
        'wrdata trace.dat ' + observations + '\nquit\n.endc\n.end\n')


def measure(rows, target):
    if (len(rows) < 30000 or rows[0][0] > 2e-12 or abs(rows[-1][0] - 60e-9) > 1e-15 or
            any(not 0 < b[0] - a[0] <= 2.001e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete sampler startup/acquisition/hold window')
    tracking = [r for r in rows if 30e-9 <= r[0] <= 39e-9]
    holding = [r for r in rows if 42e-9 <= r[0] <= 60e-9]
    disturbed = [r for r in rows if r[0] >= 50e-9]
    changed_input = target + (.25 if target <= 1.6 else -.25)
    result = {}
    for flow, index in (('physical', 1), ('schematic', 2)):
        track_error = max(abs(r[index] - target) for r in tracking)
        hold_error = max(abs(r[index] - target) for r in holding)
        hold_variation = max(r[index] for r in holding) - min(r[index] for r in holding)
        checks = dict(acquisition=track_error <= .020, held_target=hold_error <= .020,
            hold_variation=hold_variation <= .020,
            supply=all(3.2 <= r[index + 6] <= 3.4 for r in tracking + holding),
            tracking_stimulus=all(abs(r[index + 2] - target) <= .001 and r[index + 4] >= 3.2
                                  for r in tracking),
            held_input_disturbance=all(abs(r[index + 2] - changed_input) <= .001 for r in disturbed),
            clock_closed=all(abs(r[index + 4]) <= .05 for r in holding),
            rails=all(-.1 <= r[index] <= 3.4 for r in rows))
        result[flow] = dict(status='pass' if all(checks.values()) else 'fail', checks=checks,
            acquisition_error_V=track_error, held_target_error_V=hold_error,
            hold_variation_V=hold_variation)
    difference = max(abs(r[1] - r[2]) for r in rows if r[0] >= 30e-9)
    result['comparison'] = dict(status='pass' if difference <= .005 else 'fail',
        maximum_output_difference_V=difference, limit_V=.005)
    result['status'] = 'pass' if all(result[k]['status'] == 'pass'
        for k in ('physical', 'schematic', 'comparison')) else 'fail'
    return result


def run(args):
    proof = json.loads(args.proof.read_text())
    if (proof.get('status') != 'pass' or not proof['cases']['connected'].get('local_drc_pass') or
            not all(proof['cases'][case].get('missing_device_rejected')
                for case in ('missing_hold', 'missing_dummy_n', 'missing_dummy_p'))):
        raise ValueError('passing sampler geometry and missing-charge checks required')
    hashes = dict(proof['source_sha256'])
    for case in proof['cases'].values():
        for path, sha in case['artifact_sha256'].items():
            if path in hashes and hashes[path] != sha:
                raise ValueError('conflicting sampler evidence')
            hashes[path] = sha
    verify_files(hashes)
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, args.proof.resolve(), args.ngspice.resolve(), models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    for module in list(sys.modules.values()):
        filename = getattr(module, '__file__', None)
        if filename:
            path = Path(filename).resolve()
            if path.is_relative_to(ROOT) and 'build' not in path.relative_to(ROOT).parts:
                paths.append(path)
    for path in paths:
        sha = digest(path)
        if str(path) in hashes and hashes[str(path)] != sha:
            raise ValueError('changed sampler input')
        hashes[str(path)] = sha
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='local unfilled sampler PSP/MIM/RC; five static acquisition levels and held input disturbance; '
              'excludes dynamic aperture, real preamplifier/comparator and full ADC bank',
        corner=args.corner, temperature_C=args.temperature)
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
               SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    try:
        report['stage'] = 'extraction'
        save()
        with memory_slot(['receive-sampler-extracted'], exclusive=not args.concurrent):
            extraction = extract_named(Path(proof['work']) / 'connected/sampler.gds', TOP,
                PORTS, args.pdk, args.magic, work / 'extraction', substrate='VSS')
            if extraction['inventory']['models'] != {'sg13_hv_nmos': 8, 'sg13_hv_pmos': 17, 'cap_cmim': 9}:
                raise ValueError('extraction changed sampler finger/storage inventory')
            for group in ('source_sha256', 'artifact_sha256'):
                for path, sha in extraction[group].items():
                    if path in hashes and hashes[path] != sha:
                        raise ValueError('conflicting sampler extraction')
                    hashes[path] = sha
            physical = (work / 'extraction/conserved.spice').read_text()
            physical = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', physical, flags=re.M)
            schematic = reference(proof['cases']['connected']['geometry'])
            for target in (1.1, 1.35, 1.6, 1.85, 2.1):
                directory = work / f'level-{target:g}'
                directory.mkdir()
                deck = directory / 'bench.cir'
                deck.write_text(experiment(physical, schematic, models, target, args.corner, args.temperature))
                hashes[str(deck)] = digest(deck)
                report['stage'] = directory.name
                save()
                with capture_reserved_trace(directory / 'trace.dat', 8 * 1024 ** 2) as trace:
                    with (directory / 'ngspice.log').open('w') as log:
                        execute(['prlimit', '--as=536870912', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                            cwd=directory, env=env, stdout=log, stderr=-2, timeout=900,
                            check=True, memory_exclusive=False)
                with gzip.open(trace.path, 'rt') as stream:
                    header = next(stream).split()
                    if len(header) != 9 or header[0] != 'time':
                        raise ValueError('sampler observation columns changed')
                    rows = checked_rows((line.split() for line in stream if line.strip()), 9, str(trace.path))
                result = measure(rows, target)
                result['trace_storage'] = trace.metadata
                result['artifact_sha256'] = {str(trace.path): digest(trace.path)}
                report['cases'][directory.name] = result
                save()
        verify_files(hashes)
        report.update(status='pass' if all(c['status'] == 'pass' for c in report['cases'].values()) else 'fail',
                      stage='complete')
        if report['status'] != 'pass':
            raise ValueError('sampler independent or comparison limits failed')
        print('PASS five extracted sampler acquisition/hold levels and independent PSP comparison')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'magic', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, choices=(-40, 27, 125), default=27)
    parser.add_argument('--concurrent', action='store_true', help='separately budgeted 512 MiB simulator')
    run(parser.parse_args())
