"""Characterize real ADC conversion with schematic and extracted sampler charge."""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parent), str(ROOT / 'VerilogA'),
               str(ROOT / '1G/tests'), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from differential_conversion_process import process_deck
from differential_receive_conversion import contract, read_spice
from mos_geometry import size_mos_instances
from receive_sampler_integration import separate_samplers, compose
from receive_sampler_extracted import reference
from programmable_receive_gain import windowed_deck
from time_grid import windowed_transient, check_observed_grid
from reserved_trace import capture_reserved_trace
from hardware import source_dependencies
from extract_analog import digest
from input_charge import verify_files
from tool_process import configure_cleanup, execute, inherited_slot
import xspice_live


def evidence(layout_path, extraction_path):
    layout, extraction = [json.loads(p.read_text()) for p in (layout_path, extraction_path)]
    if (layout.get('status') != 'pass' or
            not layout['cases']['connected'].get('local_drc_pass') or
            not all(layout['cases'][name].get('missing_device_rejected')
                    for name in ('missing_hold', 'missing_dummy_n', 'missing_dummy_p')) or
            extraction.get('stage') != 'complete' or extraction.get('status') not in ('pass', 'fail') or
            set(extraction.get('cases', {})) != {'level-1.1', 'level-1.35', 'level-1.6', 'level-1.85', 'level-2.1'}):
        raise ValueError('complete local sampler evidence required')
    hashes = {}
    for group in [layout['source_sha256'], extraction['source_sha256']] + [
            case['artifact_sha256'] for case in layout['cases'].values()]:
        for path, sha in group.items():
            if path in hashes and hashes[path] != sha:
                raise ValueError('conflicting sampler geometry/extraction provenance')
            hashes[path] = sha
    if hashes.get(str(layout_path.resolve())) != digest(layout_path):
        raise ValueError('extraction does not attest this geometry receipt')
    physical_path = Path(extraction['work']) / 'extraction/conserved.spice'
    if str(physical_path) not in hashes:
        raise ValueError('unattested extracted sampler model')
    verify_files(hashes)
    physical = physical_path.read_text()
    physical = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', physical, flags=re.M)
    geometry = layout['cases']['connected']['geometry']
    models = dict(schematic=reference(geometry), physical=physical)
    return geometry['binding'], models, hashes, extraction['status']


def compare_codes(first, second):
    if len(first['codes']) != len(second['codes']) or not first['codes']:
        raise ValueError('incomplete ADC comparison')
    for result in (first, second):
        if (len(result['samples']) != len(result['codes']) or
                any(type(code) is not int or not 0 <= code <= 255 for code in result['codes']) or
                any(not math.isfinite(sample['target_V']) for sample in result['samples'])):
            raise ValueError('invalid ADC comparison samples')
    differences = [abs(a - b) for a, b in zip(first['codes'], second['codes'])]
    target_difference = max(abs(a['target_V'] - b['target_V'])
                            for a, b in zip(first['samples'], second['samples']))
    return dict(status='pass' if max(differences) <= 1 and target_difference <= .005 else 'fail',
        code_differences=differences, code_limit=1, maximum_capture_target_difference_V=target_difference,
        capture_target_limit_V=.005)


def run(args):
    plan, models, hashes, unit_status = evidence(args.layout, args.extraction)
    pdk_models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    files = [SOURCE, args.layout.resolve(), args.extraction.resolve(), args.bench.resolve(), args.ngspice.resolve()]
    files += [p for p in pdk_models.rglob('*') if p.is_file() and
              (p.suffix in ('.lib', '.spice', '.osdi') or p.name == '.spiceinit')]
    for module in list(sys.modules.values()):
        filename = getattr(module, '__file__', None)
        if filename:
            path = Path(filename).resolve()
            if path.is_relative_to(ROOT) and 'build' not in path.relative_to(ROOT).parts:
                files.append(path)
    hashes.update({str(p): digest(p) for p in files})
    hashes.update(source_dependencies([SOURCE.with_name('differential_receive_conversion.cpp')],
        ROOT.parent, args.cxx, ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include'), '-I/usr/include/eigen3']))
    include = xspice_live.headers(args.ngspice)
    hashes.update(source_dependencies([SOURCE.with_name('sim_adc_controller.cpp')], ROOT.parent, args.cxx,
        ['-std=c++17', '-DCPPHDL_ADC_COMPACT', '-I' + str(ROOT.parent / 'include'), '-I' + str(include)]))
    installation = args.ngspice.resolve().parents[1]
    extra = list((include / 'ngspice').glob('*.h')) + list((installation / 'lib/ngspice').glob('*.cm'))
    extra += [installation / 'share/ngspice/scripts/spinit']
    hashes.update({str(p): digest(p) for p in extra})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', stage='prepare', work=str(work), source_sha256=hashes, flows={},
        unit_electrical_status=unit_status, sampler_qualified=False, full_phy_qualified=False,
        trace_capacity_bytes=256 * 1024 ** 2,
        scope='ADC-level characterization of a locally checked sampler candidate; actual preamplifier, '
              'DAC, comparator and C++RTL feedback controller retained; does not waive failed sampler unit checks',
        slices=args.slices, gain=args.gain, corner=args.corner, temperature_C=args.temperature)

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    args.export_arguments = [args.slices, args.gain, 1.5, args.bursts]
    try:
        save()
        for flow, model in models.items():
            directory = work / flow
            directory.mkdir()

            def prepare(directory, metadata):
                original = (directory / 'bench.cir').read_text()
                deck, inventory = process_deck((directory / 'design.va').read_text(), original,
                    pdk_models / 'models', metadata, corner=args.corner, temperature=args.temperature)
                deck, _ = size_mos_instances(deck)
                host, bindings, boundary = separate_samplers(deck, inventory, metadata, plan)
                deck = compose(host, bindings, model)
                deck = deck.replace('.temp ', f'.lib "{pdk_models / "models/cornerCAP.lib"}" cap_typ\n.temp ', 1)
                deck, changed = re.subn(r'^(Vbit\d+ \S+ 0) DC ([01])$', r'\1 PWL(0 \2 1 \2)', deck, flags=re.M)
                if changed != 9 * args.slices:
                    raise ValueError('changed host control initial conditions')
                deck, grid = windowed_transient(windowed_deck(deck), 1e-9)
                if not re.search(r'^tran .*\buic\b', deck, re.M | re.I):
                    raise ValueError('sampler conversion must retain cold UIC startup')
                (directory / 'measurement.cir').write_text(deck)
                (directory / 'timing-grid.json').write_text(json.dumps(grid, indent=2) + '\n')
                report['flows'][flow] = dict(status='prepared', boundary=boundary)
                save()
                return deck, []

            report['stage'] = flow
            save()
            if args.emit_only:
                execute([args.bench.resolve(), directory, 'export', *map(str, args.export_arguments)],
                        check=True, timeout=60, memory_exclusive=False)
                prepare(directory, json.loads((directory / 'live.json').read_text()))
                report['flows'][flow]['status'] = 'netlist_only'
            else:
                with capture_reserved_trace(directory / 'spice.dat', report['trace_capacity_bytes']) as captured:
                    xspice_live.live_flow(args, directory, prepare)
                header = next(csv.reader((directory / 'columns.csv').open()))
                rows = read_spice(captured.path, header)
                with (directory / 'controls.csv').open() as stream:
                    controls = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]
                result = contract(header, rows, controls, json.loads((directory / 'live.json').read_text()))
                result['observed_grid'] = check_observed_grid(rows, json.loads((directory / 'timing-grid.json').read_text()))
                result['bridge'] = xspice_live.check_bridge(directory, args.slices,
                    lambda path, names: read_spice(captured.path, names))
                result.update(status='pass' if all(result['checks'].values()) else 'fail',
                              waveform=str(captured.path), trace_storage=captured.metadata)
                report['flows'][flow].update(result)
            report['flows'][flow]['artifact_sha256'] = {str(p): digest(p) for p in directory.iterdir() if p.is_file()}
            save()
        verify_files(hashes)
        if args.emit_only:
            report.update(status='netlists_emitted', stage='complete')
        else:
            report['comparison'] = compare_codes(report['flows']['schematic'], report['flows']['physical'])
            good = all(f['status'] == 'pass' for f in report['flows'].values()) and report['comparison']['status'] == 'pass'
            report.update(status='pass' if good else 'fail', stage='complete')
            if not good:
                raise ValueError('ADC purpose or schematic/extracted comparison failed')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout', 'extraction', 'bench', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--slices', type=int, choices=(1, 4, 13), default=1)
    parser.add_argument('--gain', type=int, choices=(4, 8), default=4)
    parser.add_argument('--bursts', type=int, choices=(1, 2), default=1)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, choices=(-40, 27, 125), default=27)
    parser.add_argument('--simulation-timeout', type=float, default=21600)
    parser.add_argument('--emit-only', action='store_true')
    args = parser.parse_args()
    if args.bursts == 2 and args.slices != 13:
        parser.error('two bursts require thirteen slices')
    if not args.emit_only and not inherited_slot():
        # The child inherits ownership; queue waiting consumes no simulation
        # timeout and cannot overlap a protected router or clock simulation.
        raise SystemExit(execute(['prlimit', '--as=3221225472', '--', sys.executable,
                                  SOURCE, *sys.argv[1:]], memory_exclusive=True).returncode)
    run(args)
