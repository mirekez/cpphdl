#!/usr/bin/env python3
"""Run the original oscillator acceptance checks with actual layout devices."""
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
sys.path[:0] = [str(ANALOG / 'VerilogA'), str(SOURCE.parents[1]), str(SOURCE.parent),
               str(ANALOG / '1G/tests'), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from oscillator_layout_process import netlist
from processing_clock_oscillator_layout import circuit, PORTS
from processing_clock_oscillator import measure, CASES, HEADER
from compact_trace import read_spice
from extract_analog import digest
from input_charge import verify_files
from tool_process import execute, configure_cleanup


def deck(physical, models, code, load, refined, probes):
    sources = ['Vsupply SUPPLY 0 1.2', 'Rsupply SUPPLY VDD 10',
               'Vclear CLEAR 0 PWL(0 1.2 32n 1.2 32.1n 0)']
    for bit in range(8):
        for kind, shift in [('COARSE', bit + 8), ('FINE', bit)]:
            sources.append(f'V{kind}{bit} {kind}_N{bit} 0 {0 if code >> shift & 1 else 1.2}')
    nodes = ['VDD', '0', *PORTS[2:]]
    observations = ['v(OUT)', 'v(Xclock.' + probes['RING'] + ')',
                    'v(Xclock.' + probes['CONTROL'] + ')', 'v(VDD)', 'v(CLEAR)']
    maximum = 5e-12 if refined else 10e-12
    return '\n'.join(['Physical oscillator process devices, typical corner',
        f'.lib "{models}/cornerMOSlv.lib" mos_tt', f'.lib "{models}/cornerRES.lib" res_typ',
        f'.lib "{models}/cornerCAP.lib" cap_typ', f'.include "{models}/diodes.lib"', '.temp 27', physical,
        *sources, 'Xclock ' + ' '.join(nodes) + ' PhysicalOscillator', f'Cload OUT 0 {load:.17g} IC=0',
        '.save ' + ' '.join(observations),
        '.options klu method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8 chgtol=1e-16',
        '.control', 'set num_threads=1', 'set noaskquit', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
        f'tran {maximum / 2:.17g} 512n 0 {maximum:.17g} uic',
        'wrdata trace.dat ' + ' '.join(observations), 'quit', '.endc', '.end', ''])


def run(args):
    proof_path = args.layout_proof.resolve()
    proof = json.loads(proof_path.read_text())
    if proof.get('status') != 'pass' or proof.get('lvs', {}).get('instantiated_devices') != 294:
        raise ValueError('requires the independently LVS/local-DRC-checked protected oscillator')
    verify_files(proof['source_sha256'])
    verify_files(proof['artifact_sha256'])
    physical, inventory = netlist(proof['geometry'])
    source = SOURCE.parents[1] / 'generated/OneGIHPProcessingClockOscillator.va'
    devices, _ = circuit(source)
    fine = next(d for d in devices if d['kind'] == 'R' and d['value'] > 1e6)
    rings = {d['gate'] for d in devices if d.get('role') in ('predriver_n', 'predriver_p')}
    if len(rings) != 1:
        raise ValueError('ambiguous oscillator ring observation')
    probes = dict(CONTROL=fine['b'], RING=next(iter(rings)))
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    files = [SOURCE, proof_path, SOURCE.parents[1] / 'oscillator_layout_process.py',
        SOURCE.with_name('processing_clock_oscillator.py'), ANALOG / 'VerilogA/compact_trace.py',
        ANALOG / 'VerilogA/compare.py', SOURCE.parents[1] / 'input_charge.py',
        SOURCE.parents[1] / 'extract_analog.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        args.ngspice.resolve()]
    files += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes = dict(proof['source_sha256'])
    hashes.update(proof['artifact_sha256'])
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={}, devices=inventory,
        scope='actual LVS-checked MOS fingers, MIM plates, nonlinear poly resistors and protection diode, TT only; '
              'excludes wiring/substrate RC, closed-loop acquisition, mismatch, noise and full chip')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        for name, (code, load, refined) in CASES.items():
            case = work / name
            case.mkdir()
            (case / 'bench.cir').write_text(deck(physical, models, code, load, refined, probes))
            environment = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models.parent))
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case, env=environment,
                    stdout=log, stderr=-2, timeout=1800, check=True, memory_exclusive=False)
            trace = case / 'trace.dat'
            metrics = measure(read_spice(trace, HEADER), refined)
            with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
                shutil.copyfileobj(source, target)
            trace.unlink()
            metrics.update(parameters=dict(trim=code, load_F=load, refined=refined), artifact_sha256={
                str(case / 'bench.cir'): digest(case / 'bench.cir'),
                str(trace) + '.gz': digest(str(trace) + '.gz')})
            report['cases'][name] = metrics
            save()
            if metrics['status'] != 'pass':
                raise ValueError('physical device oscillator failed: ' + name + ' ' + str(metrics['checks']))
        rates = [report['cases'][str(code)]['frequency_Hz'] for code in (28672, 32768, 38528, 40960)]
        if not (rates[0] < 132e6 and rates[-1] > 136e6 and all(a < b for a, b in zip(rates, rates[1:]))):
            raise ValueError('physical device trim range does not bracket the processing clock band')
        centre = report['cases']['38528']['frequency_Hz']
        for name, limit in [('loaded', .02), ('refined', .005)]:
            if abs(report['cases'][name]['frequency_Hz'] / centre - 1) > limit:
                raise ValueError('physical device load/refinement sensitivity exceeded: ' + name)
        verify_files(hashes)
        report.update(status='pass', monotonic_range_Hz=[rates[0], rates[-1]], required_band_Hz=[132e6, 136e6])
        print('PASS actual oscillator devices: original trim-range, rail, load and refinement checks')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout-proof', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
