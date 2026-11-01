#!/usr/bin/env python3
"""Check the reference PLL using the actual LVS-checked layout devices."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / 'VerilogA'), str(SOURCE.parents[1]), str(SOURCE.parent), str(ANALOG / '1G/tests'),
               str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from reference_clock_layout_process import netlist, observations, PORTS, HEADER
from reference_clock_contract import measure
from compact_trace import read_spice
from extract_analog import digest
from input_charge import verify_files
from hardware import source_dependencies
from tool_process import execute, configure_cleanup


def deck(physical, probes, models, load, refined):
    voltages = ['v(' + (probes[name] if probes[name] in PORTS else 'Xclock.' + probes[name]) + ')' for name in HEADER]
    maximum = 10e-12 if refined else 20e-12
    return '\n'.join(['Physical reference-clock devices, TT; ideal substrate contacts and no wiring RC',
        f'.lib "{models}/cornerMOSlv.lib" mos_tt', f'.lib "{models}/cornerMOShv.lib" mos_tt',
        f'.lib "{models}/cornerRES.lib" res_typ', f'.lib "{models}/cornerCAP.lib" cap_typ',
        f'.include "{models}/diodes.lib"', '.temp 27', physical,
        'Vlogic_supply LOGIC_SOURCE 0 1.2', 'Rlogic_supply LOGIC_SOURCE VLOGIC 10',
        'Clogic_supply VLOGIC 0 2p IC=0',
        'Vio_supply IO_SOURCE 0 3.3', 'Rio_supply IO_SOURCE VDD 10', 'Cio_supply VDD 0 2p IC=0',
        'Vreference REFERENCE_SOURCE 0 PULSE(0 1.2 4n 100p 100p 3.9n 8n)',
        'Rreference REFERENCE_SOURCE REF 50',
        'Vclear CLEAR_SOURCE 0 PWL(0 1.2 32n 1.2 32.1n 0)', 'Rclear CLEAR_SOURCE CLEAR 50',
        'Xclock VDD 0 VLOGIC REF CLEAR OUT OUT_N PhysicalReferenceClock',
        f'Cload OUT 0 {load:.17g} IC=0', '.save ' + ' '.join(voltages),
        '.options klu method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8 chgtol=1e-16',
        '.control', 'set num_threads=1', 'set noaskquit', 'set wr_singlescale', 'set wr_vecnames',
        'set numdgt=17', f'tran {maximum / 2:.17g} 2u 0 {maximum:.17g} uic',
        'wrdata trace.dat ' + ' '.join(voltages), 'quit', '.endc', '.end', ''])


def run(args):
    proof_path = args.layout_proof.resolve()
    proof = json.loads(proof_path.read_text())
    if (proof.get('status') != 'pass' or proof.get('full_drc_pass') is not True
            or proof.get('lvs', {}).get('instantiated_devices') != 526):
        raise ValueError('requires the complete LVS/full-DRC reference-clock proof')
    verify_files(proof['source_sha256'])
    verify_files(proof['artifact_sha256'])
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    files = [SOURCE, proof_path, SOURCE.parents[1] / 'reference_clock_layout_process.py',
        SOURCE.with_name('reference_clock_contract.py'), ANALOG / 'VerilogA/compact_trace.py',
        SOURCE.parents[1] / 'input_charge.py', SOURCE.parents[1] / 'extract_analog.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        ANALOG / '1G/tests/hardware.py', args.exporter.resolve(), args.ngspice.resolve()]
    files += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes = dict(proof['source_sha256'])
    hashes.update(proof['artifact_sha256'])
    hashes.update(source_dependencies([SOURCE.with_name('reference_clock_layout_export.cpp')],
        ANALOG.parent, args.cxx, ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')]))
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        load_F=args.load, refined=args.refined,
        scope='TT physical MOS fingers, R3 poly, MIM series/parallel banks and protection diodes; '
              'ideal substrate contacts; excludes wire/substrate RC, PVT, noise, mismatch and chip integration')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        with (work / 'export.log').open('w') as log:
            execute([str(args.exporter.resolve()), str(work)], stdout=log, stderr=-2,
                    check=True, timeout=60, memory_exclusive=False)
        generated = SOURCE.parents[1] / 'generated/OneGIHPReferenceClockGenerator.va'
        if digest(work / 'reference.va') != digest(generated):
            raise ValueError('layout export differs from the current C++ circuit')
        probes = observations((work / 'reference.va').read_text(), json.loads((work / 'probes.json').read_text()))
        physical, report['devices'] = netlist(proof['geometry'])
        report['observations'] = probes
        (work / 'physical.cir').write_text(deck(physical, probes, models, args.load, args.refined))
        env = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                   PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models.parent))
        save()
        with (work / 'ngspice.log').open('w') as log:
            execute(['prlimit', '--as=1073741824', '--', str(args.ngspice.resolve()), '-b', 'physical.cir'],
                cwd=work, env=env, stdout=log, stderr=-2, timeout=7200, check=True, memory_exclusive=False)
        trace = work / 'trace.dat'
        report['electrical'] = measure(read_spice(trace, ['time_s', *HEADER]), refined=args.refined)
        with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
            shutil.copyfileobj(source, target)
        trace.unlink()
        report['artifact_sha256'] = {str(p): digest(p) for p in
            (work / 'physical.cir', work / 'reference.va', work / 'probes.json', work / 'trace.dat.gz')}
        verify_files(hashes)
        if report['electrical']['status'] != 'pass':
            raise ValueError('physical reference-clock electrical limits failed: ' + str(report['electrical']['checks']))
        report['status'] = 'pass'
        print('PASS physical reference-clock devices; extracted wiring and chip qualification remain required')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout-proof', 'exporter', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--load', type=float, choices=(20e-15, 200e-15), default=20e-15)
    parser.add_argument('--refined', action='store_true')
    run(parser.parse_args())
