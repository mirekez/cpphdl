"""Every generated module must import its own structured port/wire types."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='imports-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-6000:]}\nArtifacts: {work}')
        return result.stdout

    runner = fixture / 'HierarchyImportsRun.cc'
    if not args.verilator:
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
             '-I' + str(include), runner, '-o', work / 'run'], 'compile')
        print(run([work / 'run'], 'run'), end='')
        return

    generated = work / 'rtl'
    run([args.cpphdl, '--generated-dir=' + str(generated), fixture / 'HierarchyImports.cc',
         '--', '-std=c++23', '-I' + str(include)], 'convert')
    packages = {path.stem: path for path in generated.glob('*_pkg.sv')}
    sources = {path.stem: path for path in generated.glob('*.sv') if path.stem not in packages}

    # Check each file independently: compilation-unit wildcard imports from an
    # earlier file must not hide a missing import in a parent or interface owner.
    expected = {
        'ImportUnboundParent': {'ImportDetachedPacket'},
        'ImportPlainLeafImportRequest': {'ImportRequest', 'ImportMeta', 'ImportKind'},
        'ImportInterfaceOnly': {'ImportRequest', 'ImportMeta', 'ImportKind', 'ImportResponse8'},
        'ImportArrayInterfaceOnly': {'ImportArrayWord'},
    }
    for module in ('ImportSource', 'ImportSink', 'ImportSourceProxy', 'ImportSinkProxy',
                   'ImportInterfaceParent', 'ImportAssignedParent'):
        expected[module] = {'ImportRequest', 'ImportMeta', 'ImportKind', 'ImportResponse16'}
    for module, types in expected.items():
        text = sources[module].read_text()
        imports = re.findall(r'\bimport\s+(\w+)_pkg::\*;', text)
        missing = types - set(imports)
        if missing or len(imports) != len(set(imports)):
            raise AssertionError(f'{module}: missing={missing}, duplicate imports={imports}; {work}')
    unbound = sources['ImportUnboundParent'].read_text()
    for wire in ('unused__packet_in', 'unused__packet_out',
                 'unused_array__packet_in', 'unused_array__packet_out'):
        if not re.search(r'\bwire\s+ImportDetachedPacket\s+' + wire + r'\b', unbound):
            raise AssertionError(f'missing unassigned wire {wire}')

    # Topologically order packages; only their explicit dependencies determine
    # order, not the order in which the converter happened to visit types.
    ordered = []
    visiting = set()
    def package(name):
        if packages[name] in ordered:
            return
        if name in visiting:
            raise AssertionError(f'package import cycle: {name}')
        visiting.add(name)
        for dependency in re.findall(r'\bimport\s+(\w+)::', packages[name].read_text()):
            package(dependency)
        visiting.remove(name)
        ordered.append(packages[name])
    for name in sorted(packages):
        package(name)
    # Put the parents before their children, rather than letting child imports
    # accidentally provide visibility to parents in Verilator's compilation unit.
    modules = [sources['HierarchyImports'], sources['ImportUnboundParent'],
               sources['ImportInterfaceParent']]
    modules += [path for path in sources.values() if path not in modules]
    for top in ('ImportInterfaceOnly', 'ImportArrayInterfaceOnly'):
        run([args.verilator, '--lint-only', '-Wno-fatal', '--top-module', top,
             *ordered, sources[top]], 'lint-' + top)
    run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
         '--top-module', 'HierarchyImports', '--Mdir', work / 'obj',
         '-CFLAGS', '-std=c++23 -DIMPORTS_VERILATOR -I' + str(include),
         *ordered, *modules, runner], 'verilator')
    print(run([work / 'obj/VHierarchyImports'], 'run'), end='')
    print('module-local package imports, unassigned wires and standalone interfaces passed')


if __name__ == '__main__':
    main()
