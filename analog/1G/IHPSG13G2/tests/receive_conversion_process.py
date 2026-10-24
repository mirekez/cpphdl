#!/usr/bin/env python3
"""Close C++RTL SAR control around the receiver's actual IHP PSP circuit."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from conversion_process import process_deck
from receive_frontend_process import dc_deck
from receive_dc_surface import Surface, surface_deck
from mos_geometry import size_mos_instances
from spice_vectors import keep_observed_voltages
from replay_consistency import check_replayed_decisions
from adc_array import digest
from psp_live import live_flow
import xspice_live
from receive_conversion import check
from hardware import configure_cleanup, execute, source_dependencies


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    model_root = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    fixture = args.fixture_source.resolve()
    hashes = source_dependencies([fixture], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    paths = [SOURCE, args.bench.resolve(), args.ngspice.resolve(), model_root / '.spiceinit',
             SOURCE.parents[1] / 'conversion_process.py', SOURCE.parents[1] / 'receive_frontend_process.py',
             SOURCE.parents[1] / 'mos_geometry.py', SOURCE.parents[1] / 'spice_vectors.py',
             SOURCE.parents[1] / 'replay_consistency.py', SOURCE.parents[1] / 'receive_dc_surface.py',
             SOURCE.parents[1] / 'psp_live.py',
             ANALOG / '1G/tests/adc_array.py', ANALOG / '1G/tests/receive_conversion.py',
             ANALOG / '1G/tests/receive_frontend.py', ANALOG / 'VerilogA/live.py', ANALOG / 'VerilogA/compare.py']
    paths += [p for p in model_root.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    if args.backend == 'xspice':
        host = SOURCE.with_name('sim_adc_controller.cpp')
        interface = xspice_live.headers(args.ngspice)
        hashes.update(source_dependencies([host], ANALOG.parent, args.cxx,
            ['-std=c++17', '-I' + str(ANALOG.parent / 'include'), '-I' + str(interface)]))
        installation = args.ngspice.resolve().parents[1]
        paths += [SOURCE.parents[1] / 'xspice_live.py', SOURCE.parents[1] / 'xspice_bridges.py',
                  installation / 'share/ngspice/scripts/spinit']
        paths += list((installation / 'lib/ngspice').glob('*.cm'))
        paths += list((interface / 'ngspice').glob('*.h'))
    hashes.update({str(p): digest(p) for p in paths})
    args.checkpoint_fingerprint = hashlib.sha256(json.dumps(dict(sources=hashes,
        slices=args.slices, bursts=args.bursts, input_drivers=args.input_drivers), sort_keys=True).encode()).hexdigest()
    report = dict(status='running', slices=args.slices, bursts=args.bursts, source_sha256=hashes,
                  same_equations_as_square_law=False,
                  fixture_source=str(fixture), backend=args.backend, input_drivers=args.input_drivers,
                  scope='closed PSP amplifier/buffer/SAR/latch/core-interface with C++RTL controller; '
                        'host-only clock and control adapters, fixed calibration, no physical translator or layout proof')
    if args.backend == 'xspice':
        report['simulation_timeout_s'] = xspice_live.simulation_timeout(args)
    dc_models = []

    def prepare(case, metadata):
        deck, inventory = process_deck((case / 'design.va').read_text(), (case / 'bench.cir').read_text(),
                                       model_root / 'models', args.slices, metadata, input_drivers=args.input_drivers)
        deck, geometry = size_mos_instances(deck)
        metadata['control_transition_s'] = .1e-9
        metadata['replay_prefix'] = args.backend == 'prefix'
        deck, replaced = re.subn(r'^(Vbit\d+ \S+ 0) DC ([01])$', r'\1 PWL(0 \2 1 \2)', deck, flags=re.M)
        if replaced != 9 * args.slices:
            raise ValueError('incomplete live PSP control sources')
        deck = keep_observed_voltages(deck.replace(' uic\n', '\n'))
        (case / 'ihp.cir').write_text(deck)
        (case / 'dc.cir').write_text(surface_deck(dc_deck(deck)))
        (case / 'binding.json').write_text(json.dumps(dict(devices=inventory, geometry=geometry), indent=2) + '\n')
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(model_root))
        with (case / 'dc.log').open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', 'dc.cir'], cwd=case, env=environment,
                    stdout=log, stderr=-2, timeout=180, check=True)
        if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found', (case / 'dc.log').read_text(), re.I):
            raise ValueError('closed PSP receive DC sweep failed')
        with (case / 'supply_surface.dat').open() as stream:
            if next(stream).split() != ['v-sweep', 'characterization_supply'] + [f'observation{i}' for i in range(6)]:
                raise ValueError('changed closed PSP receive DC observation interface')
            dc_rows = [[float(x) for x in line.split()] for line in stream if line.strip()]
        dc_models.append(Surface(dc_rows))
        report['dc_operating_points'] = len(dc_rows)
        report['dc_supply_coordinate'] = 'solved internal rail at each differential input'
        setup = [f'setcs sourcepath = ( . "{model_root / "models"}" )',
                 f"osdi '{model_root / 'osdi/psp103.osdi'}'",
                 f"osdi '{model_root / 'osdi/psp103_nqs.osdi'}'"]
        return deck, setup

    def transfer(plus, minus, supply):
        return dc_models[0](plus, minus, supply)

    try:
        if args.backend == 'xspice':
            xspice_live.live_flow(args, work, prepare)
            report.update(xspice_live.check_bridge(work, args.slices))
        else:
            live_flow(args, work, prepare)
            report.update(check_replayed_decisions(work, args.slices))
        report.update(check(work, 'veriloga', args.slices, args.bursts, transfer))
        if any(digest(Path(path)) != value for path, value in hashes.items()):
            raise ValueError('closed PSP receiver inputs changed during qualification')
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir() if p.is_file() and
            p.name not in ('checks.json', 'failed_contract.json')}
        report['status'] = 'pass'
        print('PASS closed PSP receive:', args.slices, 'slices, codes', report['codes'])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--fixture-source', type=Path, default=SOURCE.with_name('receive_process_conversion.cpp'))
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--slices', type=int, choices=(1, 4, 16), required=True)
    parser.add_argument('--bursts', type=int, choices=(1, 2), default=1)
    parser.add_argument('--input-drivers', type=int, choices=(1, 2), default=1,
                        help='explicit shared amplifier/buffer path count; circuit topology is checked')
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--prefix-timeout', type=int, help='wall-clock limit per causal PSP prefix')
    parser.add_argument('--simulation-timeout', type=float, help='positive wall-clock limit for continuous PSP simulation')
    parser.add_argument('--resume', action='store_true', help='restore the identical experiment from checkpoint.json')
    parser.add_argument('--backend', choices=('prefix', 'xspice'), default='prefix',
                        help='continuous native C++RTL events, or checkpointed causal-prefix audit')
    args = parser.parse_args()
    if args.resume and args.backend == 'xspice':
        parser.error('continuous PSP has no checkpoint; use a fresh work directory or the prefix backend')
    if args.bursts > 1 and args.slices != 16:
        parser.error('repeated operation requires sixteen slices')
    run(args)
