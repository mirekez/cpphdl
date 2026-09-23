"""Regressions for converter failures exposed by the netlist experiment."""

import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--verilator', required=True)
    args = parser.parse_args()
    fixture = (args.source_root / 'tests/netlist/WordGraph.h').read_text()
    long_name = 'WordGraph' + 'LongTemplateArgument' * 16
    cases = {
        'long_name': (long_name, fixture.replace('WordGraph', long_name)),
        'missing_init': ('WordGraph', fixture.replace('for (lane = 0;', 'for (;')),
        'missing_increment': ('WordGraph', fixture.replace('; ++lane)', ';)')
                             .replace('selected_comb = payload_in()[lane];',
                                      'selected_comb = payload_in()[lane];\n            ++lane;')),
    }
    for name, (top, source) in cases.items():
        work = args.work / name
        work.mkdir(parents=True, exist_ok=True)
        header = work / 'Input.h'
        header.write_text(source)
        generated = work / 'rtl'
        generated.mkdir(exist_ok=True)
        command = [args.cpphdl, '--generated-dir=' + str(generated), str(header),
                   '--', '-I' + str(args.source_root / 'include'), '-w']
        result = subprocess.run(command, cwd=work, capture_output=True, text=True)
        (work / 'cpphdl.log').write_text(result.stdout + result.stderr)
        if result.returncode or 'ASSERT at ' in result.stdout + result.stderr:
            raise RuntimeError(f'{name}: converter failed; see {work}')
        sources = sorted(generated.glob('*.sv'))
        if not sources or any(len(path.name) > 200 for path in sources):
            raise RuntimeError(f'{name}: missing RTL or unsafe filename')
        model = next(path for path in sources if f'module {top} (' in path.read_text())
        # Check clause positions, not the spelling of casts in the condition.
        loops = re.findall(r'\bfor\s*\(([^;]*);([^;]*);(.*?)\)\s*begin',
                           model.read_text(), re.DOTALL)
        if len(loops) != 1:
            raise RuntimeError(f'{name}: expected one procedural loop')
        init, condition, increment = (clause.strip() for clause in loops[0])
        if not re.search(r'\blane\b[^;]*<', condition):
            raise RuntimeError(f'{name}: loop condition shifted or missing')
        if name == 'missing_init':
            if init or re.sub(r'\s+', '', increment) != 'lane=lane+1':
                raise RuntimeError('Missing initializer shifted the remaining clauses')
        elif name == 'missing_increment':
            if increment or not re.match(r'lane\s*=', init):
                raise RuntimeError('Missing increment shifted the remaining clauses')
        command = [args.verilator, '--lint-only', '-Wno-fatal', '--prefix', 'VNetlistCheck',
                   '--Mdir', str(work / 'obj_dir'), str(generated / 'Predef_pkg.sv'), str(model)]
        result = subprocess.run(command, cwd=work, capture_output=True, text=True)
        (work / 'verilator.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{name}: RTL syntax validation failed; see {work}')
    print('netlist converter: long identifiers and omitted loop clauses pass')


if __name__ == '__main__':
    main()
