#!/usr/bin/env python3
"""Extract actual GDS devices, coupling capacitance and distributed resistance."""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[2] / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import configure_cleanup, execute
from extraction_capacitance import conserve


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            result.update(block)
    return result.hexdigest()


def number(value, allow_zero=False):
    match = re.fullmatch(r'([-+0-9.eE]+)([fpnum]?)', value)
    if not match:
        raise ValueError('unsupported extracted number: ' + value)
    result = float(match[1]) * {'': 1, 'f': 1e-15, 'p': 1e-12, 'n': 1e-9, 'u': 1e-6, 'm': 1e-3}[match[2]]
    if not math.isfinite(result) or result < 0 or (result == 0 and not allow_zero):
        raise ValueError('nonpositive/nonfinite extracted component')
    return result


def inventory(text, ports):
    """Fail closed on unexpected device families, collapsed fingers or ports."""
    # Magic wraps longer port lists and device records using SPICE continuation
    # lines. Normalize only those records; an orphan continuation is invalid.
    records = []
    for line in text.splitlines():
        if line.lstrip().startswith('+'):
            if not records or not records[-1].strip() or records[-1].lstrip().startswith('*'):
                raise ValueError('orphan SPICE continuation')
            records[-1] += ' ' + line.lstrip()[1:].strip()
        else:
            records.append(line)
    text = '\n'.join(records) + '\n'
    definitions = re.findall(r'^\.subckt (\S+) (.+)$', text, re.M | re.I)
    if definitions != [('ExtractedAnalog', ' '.join(ports))] or len(re.findall(r'^\.ends\s*$', text, re.M | re.I)) != 1:
        raise ValueError('extraction changed the top interface')
    models, dimensions, resistors, capacitors, names = Counter(), [], [], [], set()
    for line in text.splitlines():
        line = line.split(';')[0].strip()
        if not line or line[0] in '*.':
            continue
        fields = line.split()
        if fields[0] in names:
            raise ValueError('duplicate extracted instance')
        names.add(fields[0])
        if line[0] in 'RC':
            if len(fields) != 4 or fields[1] == fields[2]:
                raise ValueError('invalid extracted passive')
            # Magic retains some exactly zero coupling records at cthresh 0.
            # Preserve them in the report; resistors and geometry stay positive.
            value = number(fields[3], allow_zero=line[0] == 'C')
            (resistors if line[0] == 'R' else capacitors).append((fields[1], fields[2], value))
        elif line[0] == 'X':
            models_and_ports = {'sg13_lv_nmos': 4, 'sg13_lv_pmos': 4,
                                'sg13_hv_nmos': 4, 'sg13_hv_pmos': 4,
                                'cap_cmim': 2, 'rppd': 3, 'rhigh': 3, 'dantenna': 2, 'dpantenna': 2}
            model_index = next((i for i, token in enumerate(fields) if token in models_and_ports), None)
            if model_index is None or model_index != models_and_ports[fields[model_index]] + 1:
                raise ValueError('unsupported extracted device')
            model = fields[model_index]
            parameters = dict(item.split('=') for item in fields[model_index + 1:])
            if len(parameters) != len(fields) - model_index - 1:
                raise ValueError('duplicate extracted device parameter')
            width, length = number(parameters['w']), number(parameters['l'])
            minimum_length = .45e-6 if '_hv_' in model else .13e-6
            if model.startswith('sg13_') and (width > 10e-6 * (1 + 1e-10)
                                             or length < minimum_length * (1 - 1e-10)):
                raise ValueError('extraction merged fingers outside the process model geometry')
            models[model] += 1
            dimensions.append(dict(model=model, width_m=width, length_m=length))
        else:
            raise ValueError('unsupported extracted statement: ' + fields[0])
    if not models:
        raise ValueError('empty extracted circuit')
    return dict(models=dict(models), geometry=dimensions, resistors=resistors, capacitors=capacitors)


def extract(gds, top, ports, pdk, magic, work, substrate='VSS'):
    gds, pdk, magic, work = (Path(p).resolve() for p in (gds, pdk, magic, work))
    if (not ports or len(set(ports)) != len(ports)
            or any(not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', name) for name in (top, *ports))
            or any(c in str(gds) for c in '{}\n\r')):
        raise ValueError('unsupported extraction interface or path')
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise ValueError('extraction requires a fresh output directory')
    tech = pdk / 'ihp-sg13g2/libs.tech/magic'
    tools = magic.parent.parent / 'lib/magic'
    tracked = [SOURCE, SOURCE.with_name('extraction_capacitance.py'),
               SOURCE.parents[2] / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', gds, magic]
    tracked += [p for parent in (tech, tools) for p in parent.rglob('*') if p.is_file()]
    hashes = {str(p): digest(p) for p in tracked}
    report = dict(status='running', source_sha256=hashes, top=top, ports=ports,
                  scope='SG13G2 Magic distributed RC extraction; no electrical or full-chip qualification')
    try:
        commands = ['drc off', 'gds readonly true', 'gds read {' + str(gds) + '}',
                    'load ' + top, 'flatten ExtractedAnalog', 'load ExtractedAnalog',
                    'select top cell', 'expand']
        # GDS pin-purpose labels are already ports. Explicit indices retain the
        # C++ circuit's order without painting or joining any conductor.
        commands += ['port ' + name + ' index ' + str(i + 1) for i, name in enumerate(ports)]
        commands += ['extract all', 'ext2spice format ngspice', 'ext2spice scale off',
                     'ext2spice hierarchy off', 'ext2spice subcircuit top on',
                     'ext2spice merge none', 'ext2spice cthresh 0', 'ext2spice rthresh infinite',
                     'ext2spice -o capacitance.spice',
                     'extresist threshold 0', 'extresist mindelay 0',
                     'extresist minres 0', 'extresist simplify off',
                     'extresist all', 'ext2spice extresist on', 'ext2spice -o distributed.spice']
        script = 'if {[catch {\n' + '\n'.join(commands) + '\n} message]} {\nputs stderr $message\nexit 1\n}\nquit -noprompt\n'
        (work / 'extract.tcl').write_text(script)
        with (work / 'extract.log').open('w') as log:
            execute([str(magic), '-dnull', '-noconsole', '-rcfile', str(tech / 'ihp-sg13g2.magicrc'), 'extract.tcl'],
                    cwd=work, env=dict(os.environ, OMP_NUM_THREADS='1'), stdout=log, stderr=-2,
                    check=True, timeout=300, memory_exclusive=False)
        log = (work / 'extract.log').read_text()
        if re.search(r"\b(?:error|unrecognized|unknown|failed)\b|Box must be present|Couldn't", log, re.I):
            raise ValueError('extraction diagnostics require inspection')
        report['capacitance'] = inventory((work / 'capacitance.spice').read_text(), ports)
        report['distributed'] = inventory((work / 'distributed.spice').read_text(), ports)
        if report['capacitance']['models'] != report['distributed']['models']:
            raise ValueError('resistance extraction changed device inventory')
        corrected, audit = conserve(report['capacitance'], report['distributed'],
                                   (work / 'distributed.spice').read_text(), substrate)
        (work / 'conserved.spice').write_text(corrected)
        report['capacitance_conservation'] = audit
        report['conserved'] = inventory(corrected, ports)
        if any(digest(p) != expected for p, expected in hashes.items()):
            raise ValueError('extraction inputs changed during the run')
        report.update(status='pass', artifact_sha256={str(p): digest(p) for p in work.iterdir() if p.is_file()})
        return report
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'extraction.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('gds', 'pdk', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--top', required=True)
    parser.add_argument('--ports', nargs='+', required=True)
    parser.add_argument('--substrate', default='VSS')
    args = parser.parse_args()
    result = extract(args.gds, args.top, args.ports, args.pdk, args.magic, args.work, args.substrate)
    print('PASS extraction: ' + json.dumps(result['distributed']['models']))
