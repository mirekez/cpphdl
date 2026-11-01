"""Bind LVS-identified N MOSCAP geometry to its dedicated PDK charge model.

Magic recognizes the channel geometry as an LV NMOS but does not select the
dedicated MOSCAP model. The caller must supply fresh independent MOSCAP LVS
evidence. Keep four distributed terminals: their finite return wires must not
be collapsed to the ideal two-terminal schematic interface.
"""
import json
import math
import os
from pathlib import Path
import re

from extract_analog import digest, number
from extraction_capacitance import conserve
from passive_network_reduction import reduce_resistors
from tool_process import execute


def inventory(text, copies, width_um, length_um):
    if re.findall(r'^\.subckt (\S+) (.+)$', text, re.M) != [('ExtractedAnalog', 'G SUB')]:
        raise ValueError('MOSCAP extraction changed the external interface')
    if len(re.findall(r'^\.ends\s*$', text, re.M)) != 1:
        raise ValueError('MOSCAP extraction changed hierarchy')
    capacitors, resistors, devices, names = [], [], [], set()
    for line in text.splitlines():
        fields = line.split(';')[0].split()
        if not fields or fields[0][0] in '*.':
            continue
        name = fields[0]
        if name in names:
            raise ValueError('duplicate MOSCAP extraction instance')
        names.add(name)
        if name[0] in 'RC':
            if len(fields) != 4 or fields[1] == fields[2]:
                raise ValueError('invalid MOSCAP wiring element')
            value = number(fields[3], allow_zero=name[0] == 'C')
            (resistors if name[0] == 'R' else capacitors).append((fields[1], fields[2], value))
        elif name[0] == 'X':
            if len(fields) != 12 or fields[5] != 'sg13_lv_nmos':
                raise ValueError('unreviewed MOSCAP channel extraction')
            params = dict(p.split('=') for p in fields[6:])
            if set(params) != {'w', 'l', 'as', 'ad', 'ps', 'pd'}:
                raise ValueError('unreviewed MOSCAP geometry parameters')
            for key, value in params.items():
                number(value, allow_zero=key not in ('w', 'l'))
            if (not math.isclose(number(params['w']), width_um * 1e-6, rel_tol=1e-10) or
                    not math.isclose(number(params['l']), length_um * 1e-6, rel_tol=1e-10)):
                raise ValueError('MOSCAP geometry differs from independently verified LVS')
            devices.append(dict(name=name, nodes=fields[1:5], parameters=params))
        else:
            raise ValueError('unsupported MOSCAP extraction record: ' + name)
    if len(devices) != copies:
        raise ValueError('MOSCAP channel count differs from independent LVS')
    attached = {'G', 'SUB'} | {n for d in devices for n in d['nodes']}
    attached.update(n for a, b, _ in resistors for n in (a, b))
    if any(n not in attached for a, b, value in capacitors if value > 0 for n in (a, b)):
        raise ValueError('unattached MOSCAP coupling endpoint')
    return dict(models={'sg13_moscap_n': copies}, geometry=devices,
                capacitors=capacitors, resistors=resistors)


def model_wrapper(models):
    """Preserve the PDK wrapper's intrinsic geometry and model parameters."""
    models = Path(models)
    source = (models / 'sg13g2_moscap_mod.lib').read_text()
    matches = re.findall(r'^\.subckt sg13_moscap_n G SUB\n.*?^\.ends sg13_moscap_n\s*$', source, re.M | re.S)
    if len(matches) != 1:
        raise ValueError('changed dedicated MOSCAP model wrapper')
    block = matches[0]
    replacements = {
        '.subckt sg13_moscap_n G SUB': '.subckt ExtractedMOSCAP D G S B',
        'Nsg13_moscap_n SUB G SUB SUB sg13g2_moscap_n_psp': 'Nsg13_moscap_n D G S B sg13g2_moscap_n_psp',
        '.include sg13g2_moscap_parm.lib': f'.include "{models / "sg13g2_moscap_parm.lib"}"',
        '.ends sg13_moscap_n': '.ends ExtractedMOSCAP',
    }
    for old, new in replacements.items():
        if block.count(old) != 1:
            raise ValueError('changed dedicated MOSCAP terminal/model contract')
        block = block.replace(old, new, 1)
    return block + '\n'


def bind(reference_text, distributed_text, copies, width_um, length_um, models):
    if (type(copies) is not int or not 1 <= copies <= 64 or
            not all(math.isfinite(v) and 1 <= v <= 30 for v in (width_um, length_um))):
        raise ValueError('unsupported independently verified MOSCAP dimensions')
    reference = inventory(reference_text, copies, width_um, length_um)
    distributed = inventory(distributed_text, copies, width_um, length_um)
    if any(d['nodes'] != ['SUB', 'G', 'SUB', 'SUB'] for d in reference['geometry']):
        raise ValueError('MOSCAP is not the verified substrate-ground storage topology')
    parent = {}

    def root(node):
        parent.setdefault(node, node)
        if parent[node] != node:
            parent[node] = root(parent[node])
        return parent[node]

    for a, b, _ in distributed['resistors']:
        parent[root(a)] = root(b)
    if root('G') == root('SUB'):
        raise ValueError('MOSCAP wiring shorts gate and substrate')
    expected = [root(n) for n in ('SUB', 'G', 'SUB', 'SUB')]
    if any([root(n) for n in d['nodes']] != expected for d in distributed['geometry']):
        raise ValueError('MOSCAP distributed terminal disconnected or misbound')
    # Nominal diffusion areas in the dedicated PDK wrapper belong to that
    # calibrated intrinsic model. Magic's ordinary-NMOS areas are not used
    # to silently change the dedicated device model's geometry convention.
    def replace(text, net):
        devices = {d['name']: d for d in net['geometry']}
        result = []
        for line in text.splitlines():
            fields = line.split()
            if fields and fields[0] in devices:
                d = devices[fields[0]]
                line = ' '.join([d['name'], *d['nodes'], 'ExtractedMOSCAP',
                    'w=' + d['parameters']['w'], 'l=' + d['parameters']['l']])
            result.append(line)
        return '\n'.join(result) + '\n'

    corrected, conservation = conserve(reference, distributed, distributed_text, 'SUB')
    final = inventory(corrected, copies, width_um, length_um)
    if final['resistors'] != distributed['resistors'] or final['geometry'] != distributed['geometry']:
        raise ValueError('capacitance partition changed MOSCAP physical wiring/devices')
    protected = {'G', 'SUB'} | {n for d in final['geometry'] for n in d['nodes']}
    protected.update(n for a, b, _ in final['capacitors'] for n in (a, b))
    edges, reduction = reduce_resistors(final['resistors'], protected)
    lines = [line for line in corrected.splitlines() if not line.startswith('R')]
    end = lines.index('.ends')
    lines[end:end] = [f'Rwire{i} {a} {b} {r:.17g}' for i, (a, b, r) in enumerate(edges)]
    corrected = '\n'.join(lines) + '\n'
    reduced = inventory(corrected, copies, width_um, length_um)
    if reduced['geometry'] != final['geometry'] or reduced['capacitors'] != final['capacitors']:
        raise ValueError('MOSCAP wire reduction changed a device or capacitor')
    wrapper = model_wrapper(models)
    return wrapper + replace(reference_text, reference), wrapper + replace(corrected, final), dict(
        devices=copies, width_um=width_um, length_um=length_um, capacitance=conservation, reduction=reduction,
        intrinsic_diffusion_geometry='unaltered dedicated PDK MOSCAP wrapper',
        distributed_terminals_retained=4 * copies,
        scope='independently LVS-identified MOSCAP channels use dedicated PDK PSP law; no collapse of D/S/body wiring; device/field calibration and full-chip qualification separate')


def extract(gds, top, copies, width_um, length_um, pdk, magic, work):
    gds, pdk, magic, work = (Path(p).resolve() for p in (gds, pdk, magic, work))
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', top) or any(c in str(gds) for c in '{}\n\r'):
        raise ValueError('unsupported MOSCAP extraction top/path')
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise ValueError('MOSCAP extraction requires a fresh directory')
    tech = pdk / 'ihp-sg13g2/libs.tech/magic'
    models = pdk / 'ihp-sg13g2/libs.tech/ngspice/models'
    paths = [Path(__file__).resolve(), gds, magic, Path(__file__).with_name('extract_analog.py'),
             Path(__file__).with_name('extraction_capacitance.py'),
             Path(__file__).with_name('passive_network_reduction.py'),
             Path(__file__).resolve().parents[2] / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
             models / 'sg13g2_moscap_mod.lib', models / 'sg13g2_moscap_parm.lib']
    paths += [p for folder in (tech, magic.parent.parent / 'lib/magic') for p in folder.rglob('*') if p.is_file()]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', source_sha256=hashes,
        scope='MOSCAP model binding requires independent matching LVS evidence supplied by caller')
    try:
        commands = ['drc off', 'gds readonly true', 'gds read {' + str(gds) + '}',
            'load ' + top, 'flatten ExtractedAnalog', 'load ExtractedAnalog', 'select top cell', 'expand',
            'port G index 1', 'port SUB index 2', 'extract all', 'ext2spice format ngspice',
            'ext2spice scale off', 'ext2spice hierarchy off', 'ext2spice subcircuit top on',
            'ext2spice merge none', 'ext2spice cthresh 0', 'ext2spice rthresh infinite',
            'ext2spice -o capacitance.spice', 'extresist threshold 0', 'extresist mindelay 0',
            'extresist minres 0', 'extresist simplify off', 'extresist all',
            'ext2spice extresist on', 'ext2spice -o distributed.spice']
        script = 'if {[catch {\n' + '\n'.join(commands) + '\n} message]} {\nputs stderr $message\nexit 1\n}\nquit -noprompt\n'
        (work / 'extract.tcl').write_text(script)
        with (work / 'magic.log').open('w') as log:
            execute([str(magic), '-dnull', '-noconsole', '-rcfile', str(tech / 'ihp-sg13g2.magicrc'), 'extract.tcl'],
                    cwd=work, env=dict(os.environ, OMP_NUM_THREADS='1'), stdout=log, stderr=-2,
                    check=True, timeout=300, memory_exclusive=False)
        ports = re.findall(r'^port "([^"]+)" \d+ [-\d ]+ (\w+)$',
                           (work / 'ExtractedAnalog.ext').read_text(), re.M)
        if len(ports) != 2 or {p[0] for p in ports} != {'G', 'SUB'}:
            raise ValueError('dedicated MOSCAP external ports changed or duplicated')
        lumped, physical, audit = bind((work / 'capacitance.spice').read_text(),
            (work / 'distributed.spice').read_text(), copies, width_um, length_um, models)
        (work / 'lumped.spice').write_text(lumped)
        (work / 'physical.spice').write_text(physical)
        report['binding'] = audit
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('MOSCAP extraction inputs changed')
        report.update(status='pass', artifact_sha256={str(p): digest(p) for p in work.iterdir() if p.is_file()})
        return report
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
