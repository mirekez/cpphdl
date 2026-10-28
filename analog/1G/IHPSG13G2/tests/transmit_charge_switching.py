#!/usr/bin/env python3
"""Switch extracted PSP current cells with their candidate retained local storage."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parent),
               str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_current_binding import expose_storage_anchors
from transmit_physical_assembly import namespace_model
from transmit_cell_switching import measure
from input_charge import verify_files
from extract_analog import digest
from tool_process import configure_cleanup, execute, memory_slot


def run(args):
    hashes, proofs = {}, {}

    def merge(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key in ('source_sha256', 'artifact_sha256'):
                    verify_files(child)
                    for path, sha in child.items():
                        if path in hashes and hashes[path] != sha:
                            raise ValueError('conflicting switching evidence')
                        hashes[path] = sha
                else:
                    merge(child)
        elif isinstance(value, list):
            for child in value:
                merge(child)
    for name, path in (('current', args.current_proof), ('storage', args.storage_proof)):
        proof = json.loads(path.read_text())
        if proof.get('status') != 'pass' or set(proof['cases']) != {'main', 'history'}:
            raise ValueError('passing current-cell and retained-storage proofs required')
        merge(proof)
        hashes[str(path.resolve())] = digest(path)
        proofs[name] = proof
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, args.ngspice.resolve(), models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    for module in list(sys.modules.values()):
        filename = getattr(module, '__file__', None)
        if filename:
            path = Path(filename).resolve()
            if path.is_relative_to(ROOT) and 'build' not in path.relative_to(ROOT).parts:
                paths.append(path)
    for path in paths:
        value = digest(path)
        if str(path) in hashes and hashes[str(path)] != value:
            raise ValueError('stale switching input')
        hashes[str(path)] = value
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='candidate PSP current-cell realization with retained storage; TT27C, ideal gate/bias and '
              'clamped output voltages; independent current plateaus and same original comparison limits; '
              'excludes loaded translators, cold bias startup, inter-macro routing, PVT and complete TX')
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
               SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def read(path):
        path = Path(path).resolve()
        if not any(Path(p).resolve() == path and digest(path) == sha for p, sha in hashes.items()):
            raise ValueError('unqualified component circuit: ' + str(path))
        return path.read_text()

    try:
        save()
        for bank in ('main', 'history'):
            current = proofs['current']['cases'][bank]
            storage = proofs['storage']['cases'][bank]
            if (not storage['missing_capacitor_rejected'] or not storage['missing_substrate_tap_rejected'] or
                    storage['geometry']['process_realization']['storage'] != 'psp-charge'):
                raise ValueError('candidate storage requires fault rejection and explicit charge ownership')
            cell = expose_storage_anchors(read(Path(proofs['current']['work']) / bank / 'extraction/conserved.spice'), 'Cell')
            cell, cell_name = namespace_model(cell, 'Cell', 'Actual',
                ['P', 'N', 'GP', 'GN', 'BIAS', 'CASCODE', 'VSS', 'TAIL', 'LOWER'])
            ports = list(storage['geometry']['terminals'])
            retained, storage_name = namespace_model(read(Path(proofs['storage']['work']) / bank / 'extraction/conserved.spice'),
                'ExtractedAnalog', 'Storage', ports)
            nodes = {'TAIL': 'ETAIL', 'LOWER': 'ELOWER', 'VSS': '0'}
            reference = ''
            for index, device in enumerate(current['geometry']['devices']):
                width = device['geometry']['total_width_um']
                length = device['geometry']['length_um']
                if width % 5:
                    raise ValueError('unreviewed PSP folding')
                alias = dict(P='RP', N='RN', GP='GP', GN='GN', BIAS='BIAS', CASCODE='CAS', VSS='0', TAIL='TAIL', LOWER='LOWER')
                terminals = ' '.join(alias[device['nets'][p]] for p in ('D', 'G', 'S', 'B'))
                for finger in range(int(width // 5)):
                    reference += f'Xref{index}_{finger} {terminals} sg13_hv_nmos w=5u l={length:.17g}u\n'
            for index, part in enumerate(storage['geometry']['components']):
                g, connections = part['geometry'], part['connections']
                alias = dict(TAIL='TAIL', LOWER='LOWER', VSS='0')
                reference += (f'Xstore{index} {alias[connections["PLUS"]]} {alias[connections["MINUS"]]} '
                    f'cap_cmim w={g["width_um"]:.17g}u l={g["length_um"]:.17g}u\n')
            target_current = .0075 if bank == 'main' else .0025
            for voltage in (1.3, 2.3, 3.3):
                name = f'{bank}-{voltage:g}'
                target = work / name
                target.mkdir()
                point = current['dc'][f'P-{voltage:g}']['targets'][1]
                if not math.isclose(point['reference_current_A'], target_current):
                    raise ValueError('changed nominal branch-current contract')
                deck = ('* Candidate charge realization: extracted cell/storage and independent PSP/MIM reference\n'
                    f'.lib "{models / "models/cornerMOShv.lib"}" mos_tt\n'
                    f'.lib "{models / "models/cornerCAP.lib"}" cap_typ\n' + cell + retained + reference +
                    f'VEP EP 0 {voltage}\nVEN EN 0 {voltage}\nVRP RP 0 {voltage}\nVRN RN 0 {voltage}\n'
                    'VGP GP 0 PULSE(0 3.3 2n .1n .1n 7.9n 16n)\nVGN GN 0 PULSE(3.3 0 2n .1n .1n 7.9n 16n)\n'
                    f'VBIAS BIAS 0 {point["gate_V"]:.17g}\nVCAS CAS 0 1.8\n'
                    f'Xactual EP EN GP GN BIAS CAS 0 ETAIL ELOWER {cell_name}\n'
                    'Xstorage ' + ' '.join(nodes[p] for p in ports) + ' ' + storage_name + '\n'
                    # Record the four checked currents, rather than retaining
                    # every extracted internal node at every timestep.
                    '.temp 27\n.options klu\n.save i(VEP) i(VEN) i(VRP) i(VRN)\n'
                    '.control\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
                    'tran 10p 42n 0 20p\nlet ep_current = -i(VEP)\nlet en_current = -i(VEN)\n'
                    'let rp_current = -i(VRP)\nlet rn_current = -i(VRN)\n'
                    'wrdata switching.dat ep_current en_current rp_current rn_current\nquit\n.endc\n.end\n')
                (target / 'bench.cir').write_text(deck)
                report['stage'] = 'waiting_' + name
                save()
                with memory_slot(['transmit-charge-switching'], exclusive=not args.concurrent):
                    verify_files(hashes)
                    report['stage'] = name
                    save()
                    with (target / 'ngspice.log').open('w') as log:
                        execute(['prlimit', '--as=536870912', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                            cwd=target, env=env, stdout=log, stderr=-2, check=True, timeout=600, memory_exclusive=False)
                    log = (target / 'ngspice.log').read_text()
                    if 'ngspice-47 done' not in log or re.search(r'\b(error|fatal|failed|aborted)\b|timestep too small', log, re.I):
                        raise ValueError('incomplete current-cell simulation')
                    rows = [[float(v) for v in line.split()]
                            for line in (target / 'switching.dat').read_text().splitlines()[1:]]
                    result = measure(rows, target_current)
                    result['artifact_sha256'] = {str(p): digest(p) for p in
                        (target / 'bench.cir', target / 'ngspice.log', target / 'switching.dat')}
                    report['cases'][name] = result
                    save()
        verify_files(hashes)
        report.update(status='pass', stage='complete')
        print('PASS six candidate PSP current-cell/storage switching cases')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('current-proof', 'storage-proof', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--concurrent', action='store_true', help='use a separate bounded 512 MiB simulation budget')
    run(parser.parse_args())
