"""Extract and test the nonlinear-storage replica against an independent circuit."""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parent), str(ROOT / 'VerilogA'),
               str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from extract_analog import digest
from input_charge import verify_files
from named_node_extraction import extract_named
from resistor_body_extraction import adapt
from transmit_translation_bias import reference, measure
from transmit_translation_bias_layout import PORTS
from reserved_trace import capture_reserved_trace
from tool_process import configure_cleanup, execute, memory_slot


def storage_reference(geometry):
    part, = [p for p in geometry['components'] if p['kind'] == 'capacitor']
    bank = part['geometry']['bank']
    if (bank['copies'], bank['width_um'], bank['length_um'], bank['fingers'], part['connections']) != (
            220, 100, .5, 10, {'PLUS': 'BIAS', 'MINUS': 'VSS'}):
        raise ValueError('unreviewed replica storage model')
    circuit = reference(dict(geometry,
        components=[p for p in geometry['components'] if p['kind'] != 'capacitor']))
    # Multiplicity is valid for this independently wired schematic reference.
    # The extracted circuit retains every finger and its distributed terminals.
    return circuit.replace('.ends ReferenceBias',
        'Xstorage VSS BIAS VSS VSS sg13_hv_nmos w=100u l=.5u ng=10 m=220\n.ends ReferenceBias')


def experiment(physical, schematic, models, corner, temperature):
    text = ('* Actual MOS-storage replica versus independent PSP reference\n'
        f'.lib "{models / "models/cornerMOShv.lib"}" {corner}\n'
        f'.lib "{models / "models/cornerRES.lib"}" res_typ\n'
        f'.temp {temperature}\n' + physical + schematic)
    for suffix, name in (('P', 'ExtractedAnalog'), ('R', 'ReferenceBias')):
        text += (f'VS{suffix} SP{suffix} 0 3.3\nVL{suffix} SL{suffix} 0 1.2\n'
            f'RS{suffix} SP{suffix} VDD{suffix} 10\nRL{suffix} SL{suffix} L{suffix} 10\n'
            f'CS{suffix} VDD{suffix} 0 2p ic=0\nCL{suffix} L{suffix} 0 .5p ic=0\n'
            f'Xdut{suffix} VDD{suffix} 0 L{suffix} B{suffix} {name}\n')
    observations = 'v(BP) v(BR) v(VDDP) v(VDDR) v(LP) v(LR) i(VSP) i(VSR) i(VLP) i(VLR)'
    return text + ('.options klu method=gear maxord=2\n.save ' + observations + '\n.control\n'
        'set num_threads=1\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
        'tran 100p 2u 0 200p uic\nwrdata trace.dat ' + observations + '\nquit\n.endc\n.end\n')


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'pass' or set(proof['cases']) != {
            'connected', 'missing_storage', 'missing_replica'}:
        raise ValueError('passing MOS replica geometry and fault evidence required')
    connected = proof['cases']['connected']
    if connected['local_drc_violations'] or not all(proof['cases'][case]['missing_device_rejected']
            for case in ('missing_storage', 'missing_replica')):
        raise ValueError('unqualified replica geometry')
    hashes = dict(proof['source_sha256'])
    for case in proof['cases'].values():
        for path, value in case['artifact_sha256'].items():
            if path in hashes and hashes[path] != value:
                raise ValueError('conflicting replica proof')
            hashes[path] = value
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
        value = digest(path)
        if str(path) in hashes and hashes[str(path)] != value:
            raise ValueError('stale replica input: ' + str(path))
        hashes[str(path)] = value
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', stage='extraction', work=str(work), source_sha256=hashes,
        geometry=connected['geometry'], electrical={},
        physical_model=str(work / 'physical.spice'),
        exported_source=str(Path(proof['work']) / 'export/translation_bias.va'),
        scope='conserved replica RC and independent 2 us cold startup at TT/SS/FF; '
              'nominal supplies/passives; loaded bank, charge-floor PVT, fill and full TX remain open')
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
        SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    try:
        save()
        gds = Path(proof['work']) / 'connected/replica.gds'
        if str(gds) not in hashes:
            raise ValueError('replica GDS not covered by geometry proof')
        directory = work / 'extraction'
        extraction = extract_named(gds, 'TransmitTranslationBias', PORTS,
            args.pdk, args.magic, directory, substrate='VSS')
        report['extraction'] = extraction
        for path, value in extraction['source_sha256'].items():
            if path in hashes and hashes[path] != value:
                raise ValueError('replica extraction input changed')
            hashes[path] = value
        if extraction['inventory']['models'] != {'sg13_hv_nmos': 2220, 'rppd': 3}:
            raise ValueError('HV replica extracted device inventory changed')
        physical, report['resistor_body_partition'] = adapt(
            (directory / 'named/capacitance.spice').read_text(),
            (directory / 'conserved.spice').read_text(),
            (directory / 'named/ExtractedAnalog.ext').read_text(), PORTS)
        schematic = storage_reference(report['geometry'])
        (work / 'physical.spice').write_text(physical)
        report['artifact_sha256'] = {str(work / 'physical.spice'): digest(work / 'physical.spice')}
        for corner, temperature in (('mos_tt', 27), ('mos_ss', 125), ('mos_ff', -40)):
            target = work / corner
            target.mkdir()
            deck = target / 'bench.cir'
            deck.write_text(experiment(physical, schematic, models, corner, temperature))
            hashes[str(deck)] = digest(deck)
            report['stage'] = 'waiting_' + corner
            save()
            with memory_slot(['replica-mos-startup'], exclusive=not args.concurrent):
                verify_files(hashes)
                report['stage'] = corner
                save()
                with capture_reserved_trace(target / 'trace.dat', 32 * 1024 ** 2) as trace:
                    with (target / 'ngspice.log').open('w') as log:
                        execute(['prlimit', '--as=1342177280', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                            cwd=target, env=env, stdout=log, stderr=-2,
                            check=True, timeout=7200, memory_exclusive=False)
                log = (target / 'ngspice.log').read_text()
                if 'ngspice-47 done' not in log or re.search(
                        r'\b(error|fatal|failed|aborted)\b|timestep too small', log, re.I):
                    raise ValueError('incomplete replica startup: ' + corner)

                class CompressedTrace:
                    def open(self):
                        return gzip.open(trace.path, 'rt')

                report['electrical'][corner] = measure(CompressedTrace())
                report.setdefault('trace_storage', {})[corner] = trace.metadata
                report['artifact_sha256'][str(trace.path)] = digest(trace.path)
                save()
        verify_files(hashes)
        report.update(status='pass', stage='complete')
        print('PASS extracted MOS-storage replica unloaded startup at TT/SS/FF')
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
    parser.add_argument('--concurrent', action='store_true', help='use a separate bounded 1.25 GiB simulation budget')
    run(parser.parse_args())
