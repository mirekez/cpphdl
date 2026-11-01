#!/usr/bin/env python3
"""Check bias-storage charge after extracting the actual bank wiring."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parents[3] / 'pdk/1G_repeater_IHPSG13G2')]
from extract_analog import digest, extract
from extracted_connections import check_capacitor_connections
from moscap_extraction import extract as extract_moscap
from tool_process import configure_cleanup, execute


def run(args):
    dedicated = args.profile == 'dedicated'
    bank_case, bank_copies = ('forty_cells', 40) if dedicated else ('two_hundred_twenty_cells', 220)
    biases = (.85, .95, 1.05, 1.1) if dedicated else (1.6, 1.8, 2., 2.4)
    storage_floor = 400e-12 if dedicated else 50e-12
    library = 'cornerMOSCAP.lib' if dedicated else 'cornerMOShv.lib'
    corner = 'moscap_tt' if dedicated else 'mos_tt'
    evidence = json.loads(args.proof.read_text())
    if evidence.get('status') != 'pass':
        raise ValueError('passing bank geometry/connectivity evidence required')
    hashes = dict(evidence['source_sha256'])
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, args.proof.resolve(), args.ngspice.resolve(), models / '.spiceinit',
             SOURCE.parents[1] / 'extract_analog.py', SOURCE.parents[1] / 'extracted_connections.py',
             SOURCE.parents[1] / 'moscap_extraction.py',
             SOURCE.parents[3] / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    for case in ('unit', bank_case):
        record = evidence['cases'][case]
        hashes.update(record['artifact_sha256'])
        if record.get('local_drc_violations') != 0:
            raise ValueError('unqualified bank geometry')
    hashes.update({str(p): digest(p) for p in paths})
    if any(digest(p) != value for p, value in hashes.items()):
        raise ValueError('stale bank geometry evidence')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', profile=args.profile, biases_V=biases, work=str(work), source_sha256=hashes, cases={},
        scope='TT27C extracted storage bank over declared biases and 100kHz–1GHz; geometry receipt retains density failures; excludes extracted PVT, transmitter and chip qualification')
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
               SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    try:
        save()
        for case, copies in (('unit', 1), (bank_case, bank_copies)):
            target = work / case
            target.mkdir()
            record = evidence['cases'][case]
            result = report['cases'][case] = dict(copies=copies,
                retained_geometry_density_violations=record['global_density_violations'])
            gds = Path(evidence['work']) / case / 'device.gds'
            if str(gds) not in hashes:
                raise ValueError('bank GDS identity missing')
            if dedicated:
                geometry = record['geometry']
                if (geometry['model'] != 'sg13_moscap_n' or geometry['copies'] != copies
                        or geometry['width_um'] != 30 or geometry['length_um'] != 30 or not evidence.get('lvs_adapter')):
                    raise ValueError('independent dedicated MOSCAP LVS/geometry evidence missing')
                extraction = extract_moscap(gds, 'StorageBank', copies, 30, 30,
                                            args.pdk, args.magic, target / 'extraction')
            else:
                extraction = extract(gds, 'StorageBank', ['G', 'SUB'], args.pdk, args.magic,
                                     target / 'extraction', substrate='SUB')
            result['extraction'] = extraction
            for path, value in extraction['source_sha256'].items():
                if path in hashes and hashes[path] != value:
                    raise ValueError('bank extraction input changed')
                hashes[path] = value
            if not dedicated:
                if extraction['conserved']['models'] != {'sg13_hv_nmos': copies * 10}:
                    raise ValueError('extraction changed HV storage finger inventory')
                if any(not math.isclose(g['width_m'], 10e-6) or not math.isclose(g['length_m'], .5e-6)
                       for g in extraction['conserved']['geometry']):
                    raise ValueError('storage finger geometry changed')
            ext = (target / 'extraction/ExtractedAnalog.ext').read_text()
            ports = re.findall(r'^port "([^"]+)" \d+ [-\d ]+ (\w+)$', ext, re.M)
            expected_layers = {10: 'm2', 30: 'm3', 126: 'm6', 134: 'm7'}
            expected_ports = [(net, expected_layers[pin['layer']]) for net, pin in record['geometry']['terminals'].items()]
            if sorted(ports) != sorted(expected_ports):
                raise ValueError('storage external ports changed or duplicated')
            physical = (target / 'extraction' / ('physical.spice' if dedicated else 'conserved.spice')).read_text()
            lumped = (target / 'extraction' / ('lumped.spice' if dedicated else 'capacitance.spice')).read_text().replace(
                '.subckt ExtractedAnalog G SUB', '.subckt LumpedStorage G SUB')
            if not dedicated:
                result['connections'] = check_capacitor_connections(physical, ['G', 'SUB'])
            circuits = dict(extracted=physical + '\nXactual G 0 ExtractedAnalog\n',
                lumped=lumped + '\nXlumped G 0 LumpedStorage\n',
                schematic=(f'Xreference G 0 sg13_moscap_n w=30u l=30u m={copies}\n' if dedicated else
                           f'Xreference 0 G 0 0 sg13_hv_nmos w=100u l=.5u ng=10 m={copies}\n'))
            sweeps = {}
            # These circuits share only ideal independent voltage sources.
            # Separate runs preserve the experiment and bound model memory.
            for flow, circuit in circuits.items():
                experiment = target / flow
                experiment.mkdir()
                sweeps[flow] = {}
                for index, bias in enumerate(biases):
                    # Ngspice retains analysis allocations across bias changes.
                    # One process per bias bounds the full 2,200-finger bank.
                    deck = ('* Storage charge with independently driven reference circuits\n'
                        f'.lib "{models / "models" / library}" {corner}\n' + circuit +
                        f'VB G 0 dc {bias} ac 1\n.temp 27\n.options klu\n.control\n'
                        'set wr_singlescale\nset wr_vecnames\nset numdgt=17\nac dec 1 1e5 1e9\n'
                        'let storage = -imag(i(VB))/(2*pi*frequency)\n'
                        f'wrdata ac{index}.dat storage\nquit\n.endc\n.end\n')
                    bench = f'bench{index}.cir'
                    (experiment / bench).write_text(deck)
                    with (experiment / f'ngspice{index}.log').open('w') as log:
                        execute(['prlimit', '--as=1342177280', '--', str(args.ngspice.resolve()), '-b', bench],
                            cwd=experiment, env=env, stdout=log, stderr=-2, check=True, timeout=600, memory_exclusive=False)
                    raw = [[float(v) for v in line.split()]
                           for line in (experiment / f'ac{index}.dat').read_text().splitlines()[1:]]
                    if (len(raw) != 5 or any(len(r) != 3 or not all(math.isfinite(v) for v in r)
                            or r[1] <= 0 or abs(r[2]) > 1e-24 for r in raw)
                            or any(not math.isclose(r[0], 10 ** (5 + i), rel_tol=1e-10) for i, r in enumerate(raw))):
                        raise ValueError('incomplete/nonphysical storage frequency sweep')
                    sweeps[flow][bias] = raw
            result['biases'] = {}
            for bias in biases:
                rows = [[a[0], a[1], a[2], b[1], b[2], c[1], c[2]] for a, b, c in
                        zip(sweeps['extracted'][bias], sweeps['lumped'][bias], sweeps['schematic'][bias])]
                result['biases'][str(bias)] = dict(minimum_extracted_F=min(r[1] for r in rows),
                    minimum_lumped_F=min(r[3] for r in rows), minimum_reference_F=min(r[5] for r in rows),
                    maximum_wire_relative_difference=max(abs(r[1] / r[3] - 1) for r in rows),
                    maximum_schematic_relative_difference=max(abs(r[1] / r[5] - 1) for r in rows),
                    frequencies_Hz=[r[0] for r in rows], extracted_F=[r[1] for r in rows],
                    lumped_F=[r[3] for r in rows], reference_F=[r[5] for r in rows])
            result['required_storage_F'] = storage_floor * copies / bank_copies
            result['artifact_sha256'] = {str(p): digest(p) for p in target.rglob('*')
                if p.is_file() and p.suffix in ('.cir', '.dat', '.spice', '.ext')}
            save()
            if any(v['minimum_reference_F'] < result['required_storage_F'] for v in result['biases'].values()):
                raise ValueError(case + ': independent reference misses storage floor')
            if any(v['minimum_extracted_F'] < result['required_storage_F'] or v['maximum_wire_relative_difference'] > .05
                   for v in result['biases'].values()):
                raise ValueError(case + ': extracted storage fails floor or 5% wiring-loss budget')
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('storage test inputs changed')
        report['status'] = 'pass'
        print('PASS extracted', args.profile, 'storage unit/bank charge over 100kHz–1GHz')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('hv', 'dedicated'), default='hv')
    for name in ('proof', 'pdk', 'ngspice', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
