#!/usr/bin/env python3
"""Map both digital PHYs into movable cells for one placement domain.

Logical modules are reused during synthesis, then expanded by OpenROAD's link.
Only eight real SRAM masters remain macros. No previous block layout is used.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import time
from tool_process import configure_cleanup, execute

SOURCE = Path(__file__).resolve().parent
ANALOG = SOURCE.parents[1]
MACRO = 'RM_IHPSG13_2P_1024x16_c2_bm_bist'


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(part)
    return value.hexdigest()


def quote(path):
    path = str(path)
    if any(c in path for c in ('"', '\\', '\n', '\r')):
        raise ValueError('unsupported synthesis path')
    return '"' + path + '"'


def split_modules(source):
    """Partition elaborated RTLIL; retain attributes and resolved parameters."""
    modules, pending, name = {}, [], None
    autoidx = next((line + '\n' for line in source.splitlines() if line.startswith('autoidx ')), '')
    for line in source.splitlines(keepends=True):
        if line.startswith('module '):
            if name is not None:
                raise ValueError('nested RTLIL module')
            name = line.split()[1].removeprefix('\\')
            if name in modules or not re.fullmatch(r'[^\s";]+', name):
                raise ValueError('cannot partition module identity')
            pending.append(line)
        elif name is not None:
            pending.append(line)
            if line.strip() == 'end' and line.startswith('end'):
                body = ''.join(pending)
                if not re.search(r'^attribute \\blackbox 1$', body, re.M):
                    modules[name] = autoidx + body
                name, pending = None, []
        elif line.startswith('attribute '):
            pending.append(line)
    if name is not None:
        raise ValueError('incomplete RTLIL module')
    if 'RepeaterDigital' not in modules or 'SynchronousFrameFIFO' not in modules:
        raise ValueError('missing digital top or frame FIFO')
    return modules


def check_cells(design):
    modules = design['modules']
    cache, active = {}, set()

    def leaves(name):
        if name in cache:
            return cache[name]
        model = modules.get(name)
        if model is None:
            raise ValueError('unmapped cell: ' + name)
        attribute = model.get('attributes', {}).get('blackbox', 0)
        blackbox = int(attribute, 2) if isinstance(attribute, str) else int(attribute)
        if name == MACRO or name.startswith('sg13g2_'):
            if model.get('cells') or not blackbox:
                raise ValueError('non-leaf physical master: ' + name)
            result = Counter({name: 1})
        else:
            if blackbox:
                raise ValueError('logical module has no physical implementation: ' + name)
            if name in active:
                raise ValueError('recursive logical module: ' + name)
            active.add(name)
            result = Counter()
            for cell in model.get('cells', {}).values():
                result.update(leaves(cell['type']))
            active.remove(name)
        cache[name] = result
        return result

    top = modules['RepeaterDigital']
    counts = leaves('RepeaterDigital')
    if counts[MACRO] != 8:
        raise ValueError('expected eight physical frame SRAM banks')
    groups = Counter()
    for name, cell in top['cells'].items():
        groups[name.split('.', 1)[0]] += sum(leaves(cell['type']).values())
    for name in ('port_a', 'port_b', 'forwarding', 'processing_clock', 'adc_clock'):
        if not groups[name]:
            raise ValueError('lost top-level logic: ' + name)
    memories = []
    def memory_paths(name, prefix=''):
        for instance, cell in modules[name].get('cells', {}).items():
            path = prefix + instance
            if cell['type'] == MACRO:
                memories.append(path)
            elif leaves(cell['type'])[MACRO]:
                memory_paths(cell['type'], path + '/')
    memory_paths('RepeaterDigital')
    return dict(cells=dict(counts), origin_groups=dict(groups), total_instances=sum(counts.values()),
                sram_instances=sorted(memories), hierarchy_expansion='OpenROAD link_design',
                movable_standard_cells=sum(counts.values()) - 8, fixed_logic_modules=0)


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'pass':
        raise ValueError('current digital integration proof required')
    hashes = dict(proof['source_sha256'])
    hashes.update(proof['rtl_sha256'])
    inputs = args.proof.resolve().parent / 'rtl'
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('digital integration proof is stale')
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    refs = args.pdk.resolve() / 'ihp-sg13g2/libs.ref'
    liberty = refs / 'sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    memory_lib = refs / f'sg13g2_sram/lib/{MACRO}_typ_1p20V_25C.lib'
    rules = ANALOG / '1G/IHPSG13G2/frame_sram.rules'
    mapping = ANALOG / '1G/IHPSG13G2/frame_sram_map.v'
    tools = args.tools.resolve() / 'bin'
    for p in [Path(__file__), SOURCE / 'tool_process.py', args.proof.resolve(), liberty, memory_lib,
              rules, mapping, *[tools / name for name in ('sv2v', 'yosys', 'abc')]]:
        hashes[str(p)] = digest(p)
    report = dict(status='running', stage='starting', source_sha256=hashes, stages=[],
                  scope='Single-placement digital netlist; analog devices, pads, routing and signoff remain separate.',
                  full_chip_qualified=False)

    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')

    def command(name, argv, output=None, timeout=1800):
        report['stage'] = name
        save()
        print('unified/' + name, flush=True)
        start = time.monotonic()
        with (work / (name + '.log')).open('w') as log:
            stdout = output.open('w') if output else log
            try:
                result = execute(['prlimit', '--as=3221225472', '--', *map(str, argv)], cwd=work,
                    env=dict(os.environ, OMP_NUM_THREADS='1'), stdout=stdout, stderr=log,
                    timeout=timeout, check=False, memory_exclusive=True)
            finally:
                if output:
                    stdout.close()
        report['stages'].append(dict(name=name, seconds=round(time.monotonic() - start, 3), returncode=result.returncode))
        save()
        if result.returncode:
            raise RuntimeError(name + ' failed: ' + str(work / (name + '.log')))

    save()
    try:
        command('memory-convert', [tools / 'sv2v', '+RTS', '-N1', '-RTS', inputs / 'Predef_pkg.sv',
                                  inputs / 'SynchronousFrameFIFO.sv'], work / 'memory-input.v')
        (work / 'memory.ys').write_text(f'''read_verilog {quote(work / 'memory-input.v')}
hierarchy -top SynchronousFrameFIFO
proc
opt
memory_dff
memory_collect
memory_bram -rules {quote(rules)}
techmap -map {quote(mapping)}
opt_clean
write_verilog {quote(work / 'memory-mapped.v')}
''')
        command('memory-map', [tools / 'yosys', '-s', work / 'memory.ys'])
        if len(re.findall(r'\b' + MACRO + r'\s', (work / 'memory-mapped.v').read_text())) != 4:
            raise ValueError('frame memory failed physical mapping')
        rtl = [inputs / 'Predef_pkg.sv', *sorted(p for p in inputs.glob('*.sv')
            if p.name not in ('Predef_pkg.sv', 'SynchronousFrameFIFO.sv')), work / 'memory-mapped.v']
        command('convert', [tools / 'sv2v', '+RTS', '-N1', '-RTS', *rtl], work / 'design.v')
        # Separate processes bound the compiler's temporary expression graph.
        # These logical boundaries disappear before physical placement.
        (work / 'interfaces.ys').write_text(f'''read_liberty -lib {quote(memory_lib)}
read_verilog {quote(work / 'design.v')}
hierarchy -check -top RepeaterDigital
write_ilang {quote(work / 'elaborated.il')}
blackbox
write_ilang {quote(work / 'interfaces.il')}
''')
        command('interfaces', [tools / 'yosys', '-s', work / 'interfaces.ys'])
        modules = split_modules((work / 'elaborated.il').read_text())
        mapped = []
        module_root = work / 'modules'
        module_root.mkdir(exist_ok=True)
        for name in sorted(modules, key=lambda n: (-len(modules[n]), n)):
            safe_name = re.sub(r'[^A-Za-z0-9_]', '_', name)
            if safe_name != name:
                safe_name = safe_name[:60] + '_' + hashlib.sha256(name.encode()).hexdigest()[:12]
            directory = module_root / safe_name
            directory.mkdir(exist_ok=True)
            source = directory / 'input.il'
            source.write_text(modules[name])
            output = directory / 'mapped.v'
            script = directory / 'synth.ys'
            script.write_text(f'''read_liberty -lib {quote(liberty)}
read_liberty -lib {quote(memory_lib)}
read_ilang -lib {quote(work / 'interfaces.il')}
read_ilang -overwrite {quote(source)}
hierarchy -check -top {name}
synth -top {name} -noabc -noshare
dfflibmap -liberty {quote(liberty)}
abc -exe {quote(tools / 'abc')} -liberty {quote(liberty)} -D 4000
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
check -assert
select {name}
write_verilog -selected -noattr -noexpr {quote(output)}
''')
            dependencies = {str(p): digest(p) for p in
                (source, script, work / 'interfaces.il', liberty, memory_lib,
                 tools / 'yosys', tools / 'abc', SOURCE / 'tool_process.py')}
            receipt = directory / 'checks.json'
            cached = json.loads(receipt.read_text()) if receipt.exists() else {}
            if (cached.get('source_sha256') == dependencies and output.exists() and
                    cached.get('output_sha256') == digest(output)):
                print('unified/reuse-' + name, flush=True)
            else:
                command('map-' + safe_name, [tools / 'yosys', '-s', script], timeout=3600)
                receipt.write_text(json.dumps(dict(source_sha256=dependencies,
                    output_sha256=digest(output)), indent=2) + '\n')
            mapped.append(output)
        (work / 'synth.ys').write_text(f'''read_liberty -lib {quote(liberty)}
read_liberty -lib {quote(memory_lib)}
''' + ''.join(f'read_verilog {quote(p)}\n' for p in mapped) + f'''
hierarchy -check -top RepeaterDigital
check -assert
write_verilog -noattr -noexpr {quote(work / 'mapped.v')}
write_json {quote(work / 'mapped.json')}
''')
        command('link', [tools / 'yosys', '-s', work / 'synth.ys'], timeout=3600)
        report.update(check_cells(json.loads((work / 'mapped.json').read_text())))
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('unified synthesis inputs changed')
        report.update(status='mapped_unplaced', stage='complete',
                      artifact_sha256={str(work / n): digest(work / n) for n in ('mapped.v', 'mapped.json')})
        print('Mapped', report['movable_standard_cells'], 'movable cells and eight SRAMs; no fixed PHY/PCS/DSP boxes', flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
