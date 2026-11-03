#!/usr/bin/env python3
"""Independently check physical and schematic TX translator output loading."""
import argparse
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'),
               str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_translator_layout import PORTS, realization
from extract_analog import digest
from level_shifter import contracts
from tool_process import configure_cleanup, execute


def reference(source, storage='declared'):
    devices, capacitors, _ = realization(source, storage)
    lines = ['.subckt ReferenceTranslator ' + ' '.join(PORTS)]
    for index, device in enumerate(devices):
        # Match the physical 5 um fingers without copying extracted wiring.
        count = math.ceil(device['width_um'] / 5)
        for finger in range(count):
            lines.append(f'Xmos{index}_{finger} ' + ' '.join(device[k] for k in ('a', 'gate', 'b', 'body')) +
                f' {device["model"]} w={device["width_um"] / count}u l={device["length_um"]}u')
    lines += [f'Cstorage{index} {a} {b} {value:.17g}'
              for index, (a, b, value) in enumerate(capacitors)]
    return '\n'.join(lines + ['.ends ReferenceTranslator', ''])


def bench(model, name, models, load, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff') or not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported translator MOS corner or temperature')
    wave, previous = [(0, 0)], 0
    for symbol in range(1, 16):
        value = 1.2 if symbol & 1 else 0
        wave += [(32e-9 + symbol * 8e-9, previous), (32e-9 + symbol * 8e-9 + .1e-9, value)]
        previous = value
    text = (f'* Independently supplied transmitter translator, {corner}/{temperature:g}C\n'
        f'.lib "{models / "models/cornerMOSlv.lib"}" {corner}\n'
        f'.lib "{models / "models/cornerMOShv.lib"}" {corner}\n'
        f'.lib "{models / "models/cornerCAP.lib"}" cap_typ\n.temp {temperature:g}\n' + model +
        f'Xdut VDD 0 VLOGIC BIAS A Y Y_N {name}\n'
        'VSUP SOURCE 0 3.3\nVLOGIC_SOURCE LSOURCE 0 1.2\n'
        'RSUP SOURCE VDD 10\nRLOGIC LSOURCE VLOGIC 10\n'
        'CSUP VDD 0 2p\nCLOGIC VLOGIC 0 .5p\n'
        'VBIAS_SOURCE BSOURCE 0 1.8\nRBIAS BSOURCE BIAS 100\nCBIAS BIAS 0 1p\n'
        'VCMD COMMAND 0 PWL(' + ' '.join(f'{t:.17g} {v}' for t, v in wave) + ')\n'
        'RINPUT COMMAND A 50\n' + f'CLOAD Y 0 {load:.17g}\nRLOAD Y 0 10000\n')
    # Probe every terminal of every actual LV device, including distributed
    # wire nodes. No private-node number is inferred from cell ordering.
    pairs = set()
    for line in model.splitlines():
        f = line.split()
        if len(f) >= 6 and f[0].startswith('X') and f[5] == 'sg13_lv_nmos':
            pairs.update((f[2], f[i]) for i in (1, 3, 4))
        elif len(f) >= 6 and f[0].startswith('X') and f[5] == 'sg13_lv_pmos':
            pairs.update((f[2], f[i]) for i in (1, 3, 4))
    if not pairs:
        raise ValueError('translator LV exposure inventory is empty')
    external = {p: p for p in PORTS} | {'VSS': '0'}
    node = lambda name: external.get(name, 'Xdut.' + name)
    exposure = ['v(' + node(a) + (',' + node(b) if node(b) != '0' else '') + ')'
                for a, b in sorted(pairs)]
    terminals = sorted({node(n) for pair in pairs for n in pair} - {'0'})
    observables = ['v(Y)', 'v(Y_N)', 'v(A)', 'v(VDD)', 'v(VLOGIC)', 'v(COMMAND)']
    text += ('.options klu method=gear maxord=2\n.save ' +
        ' '.join(observables + ['v(' + n + ')' for n in terminals]) + '\n'
        '.control\nset num_threads=1\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
        'tran 7.8125p 160n 0 7.8125p uic\nwrdata trace.dat ' + ' '.join(observables + exposure) +
        '\nquit\n.endc\n.end\n')
    return text


def measure(path):
    with path.open() as file:
        next(file)
        rows = [[float(v) for v in line.split()] for line in file if line.strip()]
    if (not rows or len(rows[0]) < 8 or any(len(row) != len(rows[0]) or
            not all(math.isfinite(v) for v in row) for row in rows)):
        raise ValueError('missing or nonfinite transistor exposure')
    result = contracts([row[:7] for row in rows], 1.2, 3.3, True, startup=32e-9)
    exposure = max(abs(value) for row in rows for value in row[7:])
    result['maximum_lv_gate_difference_V'] = exposure
    result['checks']['lv_gate_exposure'] = exposure <= 1.65
    if not all(result['checks'].values()):
        raise ValueError('translator violates absolute limits: ' + str(result['checks']))
    return result


def compare(first, second):
    a, b = first['output_crossings'], second['output_crossings']
    if len(a) != len(b) or not a or any(x[:2] != y[:2] for x, y in zip(a, b)):
        raise ValueError('physical translator changes the output edge sequence')
    edge = max(abs(x[2] - y[2]) for x, y in zip(a, b))
    differences = [x - y for a, b in zip(first['comparison_probes_V'], second['comparison_probes_V'])
                   for x, y in zip(a, b)]
    rms = math.sqrt(sum(v * v for v in differences) / len(differences))
    # Layout allowance: 100 ps is 1.25% of the 8 ns symbol interval.
    # The independent 2 ns settling/rail and device-exposure limits still apply.
    if edge > 100e-12 or rms > .15:
        raise ValueError(f'translator layout departure exceeds 100 ps/150 mV: {edge}, {rms}')
    return dict(maximum_edge_difference_s=edge, waveform_rms_difference_V=rms,
                edge_limit_s=100e-12, waveform_limit_V=.15)


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'pass' or not proof.get('missing_device_rejected'):
        raise ValueError('passing translator geometry and fault rejection required')
    hashes = proof['source_sha256'] | proof['artifact_sha256']
    hashes.update({str(p.resolve()): digest(p) for p in (SOURCE, args.proof, args.ngspice,
        ANALOG / '1G/tests/level_shifter.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py')})
    if any(digest(p) != value for p, value in hashes.items()):
        raise ValueError('stale translator geometry proof')
    original = Path(proof['work'])
    physical = (original / 'extraction/conserved.spice').read_text()
    physical = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', physical, flags=re.M)
    implementation = proof['geometry']['process_realization']
    schematic = reference(original / 'export/translator.va', implementation['storage'])
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        process_realization=implementation,
        corner=args.corner, temperature_C=args.temperature,
        scope='selected MOS/temperature corner with 0.2/2 pF loads, nominal capacitors, finite supplies and '
              '1.8 V bias source; excludes supply/passive corners, shared replica startup, full TX and chip qualification')
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
               SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    try:
        save()
        for load in (.2e-12, 2e-12):
            case = dict(load_F=load, flows={})
            report['cases'][str(load)] = case
            for flow, model, name in [('schematic', schematic, 'ReferenceTranslator'),
                                     ('physical', physical, 'ExtractedAnalog')]:
                directory = work / (flow + '_' + str(load))
                directory.mkdir()
                deck = directory / 'bench.cir'
                deck.write_text(bench(model, name, models, load, args.corner, args.temperature))
                with (directory / 'ngspice.log').open('w') as log:
                    execute(['prlimit', '--as=1073741824', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                        cwd=directory, env=env, stdout=log, stderr=-2, check=True, timeout=900, memory_exclusive=False)
                raw = directory / 'trace.dat'
                result = measure(raw)
                original_hash = digest(raw)
                archive = raw.with_suffix('.dat.gz')
                with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as output:
                    shutil.copyfileobj(source, output)
                check = hashlib.sha256()
                with gzip.open(archive, 'rb') as source:
                    for chunk in iter(lambda: source.read(1048576), b''):
                        check.update(chunk)
                if check.hexdigest() != original_hash:
                    raise ValueError('translator trace archive changed samples')
                raw.unlink()
                result.update(artifact_sha256={str(p): digest(p) for p in (deck, archive)},
                              uncompressed_sha256=original_hash)
                case['flows'][flow] = result
                save()
                print(flow, load, 'absolute pass', flush=True)
            try:
                case['comparison'] = dict(status='pass', **compare(
                    case['flows']['schematic'], case['flows']['physical']))
            except ValueError as error:
                # Preserve the failure and still characterize the other load.
                # A failed comparison can never produce a passing receipt.
                case['comparison'] = dict(status='fail', error=str(error))
            save()
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('translator qualification inputs changed')
        if any(case['comparison']['status'] != 'pass' for case in report['cases'].values()):
            raise ValueError('translator layout comparison failed; see per-load results')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'ngspice', 'proof', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    run(parser.parse_args())
