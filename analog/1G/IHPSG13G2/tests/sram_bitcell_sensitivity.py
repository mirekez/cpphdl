#!/usr/bin/env python3
"""Check bitcell read/write/retention and resistor-width sensitivity over 90 PSP cases.

This is a local schematic experiment. It does not qualify a complete SRAM,
its peripheral circuits, extracted interconnect, or the final ASIC.
"""
import argparse
from pathlib import Path
import gzip
import hashlib
import itertools
import json
import math
import os
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from sram_reference import reachable, rewrite_resistors, MACRO, BITCELL, RESISTORS
from tool_process import execute, configure_cleanup
from input_charge import verify_files


def template(pdk):
    MODELS = pdk / 'ihp-sg13g2/libs.tech/ngspice'
    cdl = pdk / 'ihp-sg13g2/libs.ref/sg13g2_sram/cdl' / (MACRO + '.cdl')
    blocks, _ = reachable(cdl.read_text(), MACRO)
    cell = rewrite_resistors(blocks[BITCELL.upper()], RESISTORS, '2.6e-07')
    ports = cell.splitlines()[0].split()[2:]
    lines = cell.splitlines()[1:-1]
    if len(ports) != 16 or sum(line.startswith('M') for line in lines) != 8:
        raise ValueError('unexpected bitcell interface or MOS inventory')
    def pulse(begin, end):
        return f'PWL(0 0 {begin}n 0 {begin + .02}n {{supply}} {end}n {{supply}} {end + .02}n 0)'

    def deck(width, name):
        output = ['.subckt ' + name + ' ' + ' '.join(ports)]
        for line in lines:
            if line.startswith('M'):
                output.append('X' + re.sub(r' nrd=0 nrs=0$', '', line))
            else:
                match = re.fullmatch(r'(R\d+) (\S+) (\S+) res_metal([23]) w=2.6e-07 l=6e-07', line)
                if not match:
                    raise ValueError('unexpected resistor specification')
                output.append(f'{match[1]} {match[2]} {match[3]} {{.088 * .60 / {width}}}')
        output += ['.ends', f'X{name} ' + ' '.join('vdd' if p in ('NW', 'VDD') else '0' if p in ('PW', 'VSS!', 'VSS') else name + '_' + p for p in ports) + ' ' + name]
        for port in 'AB':
            for polarity in ('BLC', 'BLT'):
                node = f'{name}_{port}_{polarity}_TOP'
                rail = 'vdd' if (port == 'A') == (polarity == 'BLC') else '0'
                output += [f'Swrite_{name}_{port}_{polarity} {node} {rail} write_{port} 0 drive',
                           f'Spre_{name}_{port}_{polarity} {node} vdd precharge 0 drive',
                           f'Cline_{name}_{port}_{polarity} {node} 0 100f']
            output += [f'Rword_{name}_{port} word_{port} {name}_{port}_LWL 50',
                       f'Cword_{name}_{port} {name}_{port}_RWL 0 100f']
        output += [f'.ic v(x{name}.NT)=0 v(x{name}.NC)={{supply}}']
        return '\n'.join(output)

    text = ('* Local sensitivity only: ideal precharge/write drivers, 100 fF line loads\n'
            f'.lib "{MODELS / "models/cornerMOSlv.lib"}" mos_tt\n'
            '.param supply=1.2\n.temp 27\n.model drive SW(Ron=50 Roff=1e12 Vt=.6 Vh=.05)\n'
            'VDD vdd 0 {supply}\n'
            f'VWA write_A 0 {pulse(.9, 2.1)}\nVWB write_B 0 {pulse(4.9, 6.1)}\n'
            'VPA word_A 0 PWL(0 0 1n 0 1.02n {supply} 2n {supply} 2.02n 0 7n 0 7.02n {supply} 8n {supply} 8.02n 0)\n'
            'VPB word_B 0 PWL(0 0 3n 0 3.02n {supply} 4n {supply} 4.02n 0 5n 0 5.02n {supply} 6n {supply} 6.02n 0)\n'
            'VPRE precharge 0 PWL(0 {supply} .7n {supply} .72n 0 2.3n 0 2.32n {supply} 2.8n {supply} 2.82n 0 6.3n 0 6.32n {supply} 6.8n {supply} 6.82n 0)\n'
            + deck(.26, 'reference') + '\n' + deck(.20, 'layout_width') + '\n'
            '.control\nset num_threads=1\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
            'tran 2p 10n 0 5p uic\n'
            'wrdata trace.dat v(xreference.nt) v(xreference.nc) v(xlayout_width.nt) v(xlayout_width.nc) '
            'v(reference_B_BLC_TOP) v(reference_B_BLT_TOP) v(layout_width_B_BLC_TOP) v(layout_width_B_BLT_TOP) '
            'v(reference_A_BLC_TOP) v(reference_A_BLT_TOP) v(layout_width_A_BLC_TOP) v(layout_width_A_BLT_TOP)\n'
            'quit\n.endc\n.end\n')
    return text


def run(args):
    PDK = args.pdk.resolve()
    MODELS = PDK / 'ihp-sg13g2/libs.tech/ngspice'
    NGSPICE = args.ngspice.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    WORK = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    cdl = PDK / 'ihp-sg13g2/libs.ref/sg13g2_sram/cdl' / (MACRO + '.cdl')
    sheet_model = PDK / 'ihp-sg13g2/libs.tech/klayout/tech/lvs/rule_decks/res_extraction.lvs'
    for metal in (2, 3):
        match = re.search(r'^RSH_RES_METAL' + str(metal) + r'\s*=\s*([\d.]+)', sheet_model.read_text(), re.M)
        if match is None or float(match[1]) != .088:
            raise ValueError('nominal metal sheet model changed; revalidate the fixture')
    template_text = template(PDK)
    template_path = WORK / 'template.cir'
    template_path.write_text(template_text)
    def digest(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()
    paths = [Path(__file__).resolve(), template_path, SOURCE.parents[1] / 'sram_reference.py', cdl, sheet_model, NGSPICE, MODELS / '.spiceinit']
    paths += [p for p in MODELS.rglob('*') if p.is_file() and p.suffix in ('.lib', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', scope=__doc__, source_sha256=hashes, full_sram_qualified=False,
                  cases=[], independent_limits=dict(state_rail_fraction=.1, read_margin_fraction=.1,
                                                    maximum_waveform_difference_fraction=.01,
                                                    maximum_write_latency_s=.88e-9),
                  limitations=['schematic PSP diffusion', 'ideal peripheral drive and precharge',
                               '100 fF line loads', 'nominal 0.088 ohm/square metal',
                               'no full array, peripheral circuit, extracted RC or macro timing proof'])
    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (WORK / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)
    save()
    try:
        for corner, temperature, supply, bit in itertools.product(('tt', 'ss', 'ff', 'sf', 'fs'),
                                                                  (-40, 27, 125), (1.08, 1.2, 1.32), (0, 1)):
            case = WORK / f'{corner}_{temperature}_{supply}_{bit}'
            case.mkdir()
            text = template_text.replace(' mos_tt\n', f' mos_{corner}\n')
            text = text.replace('.param supply=1.2', f'.param supply={supply}').replace('.temp 27', f'.temp {temperature}')
            if bit == 0:
                text = re.sub(r'\.ic v\((x\w+)\.NT\)=0 v\(\1\.NC\)=\{supply\}',
                              r'.ic v(\1.NT)={supply} v(\1.NC)=0', text)
            else:
                text = re.sub(r'^(Swrite_\S+ \S+) (0|vdd)( write_[AB] 0 drive)$',
                              lambda m: m[1] + (' vdd' if m[2] == '0' else ' 0') + m[3], text, flags=re.M)
            (case / 'bench.cir').write_text(text)
            environment = dict(os.environ, PDK_ROOT=str(PDK), PDK='ihp-sg13g2',
                               SPICE_USERINIT_DIR=str(MODELS), OMP_NUM_THREADS='1')
            with (case / 'ngspice.log').open('w') as log:
                result = execute(['prlimit', '--as=536870912', '--', NGSPICE, '-b', 'bench.cir'],
                                 cwd=case, env=environment, stdout=log, stderr=-2, timeout=120,
                                 check=False, memory_exclusive=False)
            if result.returncode or re.search(r'(?im)\b(error|fatal|warning):', (case / 'ngspice.log').read_text()):
                raise ValueError('failed or warned simulation: ' + case.name)
            trace = case / 'trace.dat'
            rows = [list(map(float, line.split())) for line in trace.read_text().splitlines()[1:] if line.strip()]
            if (not rows or any(len(r) != 13 or not all(map(math.isfinite, r)) for r in rows)
                    or rows[-1][0] < 9.999e-9
                    or any(not 0 < b[0] - a[0] <= 5.001e-12 for a, b in zip(rows, rows[1:]))):
                raise ValueError('incomplete waveform: ' + case.name)
            def sample(time):
                row = min(rows, key=lambda r: abs(r[0] - time))
                if abs(row[0] - time) > 5.001e-12:
                    raise ValueError('missing measurement window')
                return row
            for time, expected in ((1.9e-9, bit), (2.2e-9, bit), (5.9e-9, 1-bit),
                                   (6.2e-9, 1-bit), (9.9e-9, 1-bit)):
                row = sample(time)
                for nt, nc in ((1, 2), (3, 4)):
                    if max(abs(row[nt] - supply*expected), abs(row[nc] - supply*(1-expected))) > .1*supply:
                        raise ValueError('write or retention failed: ' + case.name)
            margins = []
            for time, expected, pairs in ((3.9e-9, bit, ((5, 6), (7, 8))),
                                          (7.9e-9, 1-bit, ((9, 10), (11, 12)))):
                row = sample(time)
                for bc, bt in pairs:
                    margin = (row[bt] - row[bc]) * (1 if expected else -1)
                    if margin < .1*supply:
                        raise ValueError('independent read decision failed: ' + case.name)
                    margins.append(margin)
            latencies = []
            for start, expected in ((1.02e-9, bit), (5.02e-9, 1-bit)):
                for nt in (1, 3):
                    crossing = next((r[0] for r in rows if start <= r[0] <= start + .88e-9
                                     and (r[nt] >= supply/2) == bool(expected)), None)
                    if crossing is None:
                        raise ValueError('write latency failed: ' + case.name)
                    latencies.append(crossing - start)
            difference = max(abs(row[i] - row[i+2]) for row in rows for i in (1, 2, 5, 6, 9, 10))
            if difference > .01*supply:
                raise ValueError('resistor variant sensitivity exceeds budget: ' + case.name)
            archive = trace.with_suffix('.dat.gz')
            with trace.open('rb') as source, gzip.open(archive, 'wb') as target:
                target.write(source.read())
            trace_digest = digest(trace)
            with gzip.open(archive, 'rb') as source:
                if hashlib.sha256(source.read()).hexdigest() != trace_digest:
                    raise ValueError('waveform compression failed')
            trace.unlink()
            report['cases'].append(dict(corner=corner, temperature_C=temperature, supply_V=supply,
                first_write_bit=bit, read_margin_min_V=min(margins), write_latency_max_s=max(latencies),
                waveform_difference_max_V=difference, waveform_sha256=trace_digest,
                artifact_sha256={str(p): digest(p) for p in (archive, case / 'bench.cir', case / 'ngspice.log')}))
            save()
        verify_files(hashes)
        report.update(status='diagnostic_complete', cases_passed=len(report['cases']))
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()

    print('PASS 90 local PSP bitcell cases; full SRAM qualification remains separate')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
