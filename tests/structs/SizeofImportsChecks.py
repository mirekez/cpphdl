"""sizeof-only struct dependencies must be imported by each owning SV module."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def imports(path):
    # Inspect declarations, not arbitrary substrings or comments.
    return [line.strip().split()[1].split('::')[0]
            for line in path.read_text().splitlines() if line.strip().startswith('import ')]


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
    work = Path(tempfile.mkdtemp(prefix='sizeof-imports-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-8000:]}\nArtifacts: {work}')
        return result.stdout

    runner = fixture / 'SizeofImportsRun.cc'
    if not args.verilator:
        executable = work / 'run'
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
             '-I' + str(include), runner, '-o', executable], 'compile')
    else:
        generated = work / 'rtl'
        run([args.cpphdl, '--generated-dir=' + str(generated), fixture / 'SizeofImports.cc',
             '--', '-std=c++23', '-I' + str(include)], 'convert')
        expected = {
            'SizeofTypeOnly': {'SizeofTypes_Word_pkg'},
            'SizeofTemplateOnly': {'SizeofTypes_Bytes3_pkg', 'SizeofTypes_Bytes5_pkg'},
            'SizeofExprOnly': {'SizeofTypes_Word_pkg', 'SizeofTypes_Nested_pkg'},
            'SizeofImports': {'SizeofTypes_ParentOnly_pkg'},
        }
        for module, needed in expected.items():
            actual = imports(generated / (module + '.sv'))
            if not needed.issubset(actual) or len(actual) != len(set(actual)):
                raise AssertionError(f'{module}: expected {needed}, imports {actual}\nArtifacts: {work}')

        # Sort packages by declared dependencies, including nested-only types.
        ordered = []
        visiting = set()
        def package(name):
            path = generated / (name + '.sv')
            if path in ordered:
                return
            if name in visiting:
                raise AssertionError(f'cyclic package imports: {name}')
            visiting.add(name)
            for dependency in imports(path):
                package(dependency)
            visiting.remove(name)
            ordered.append(path)
        for path in sorted(generated.glob('*_pkg.sv')):
            package(path.stem)

        # Compile leaves in isolation: imports from a sibling compilation unit
        # must not accidentally make their sizeof types visible.
        for module in ('SizeofTypeOnly', 'SizeofTemplateOnly', 'SizeofExprOnly'):
            run([args.verilator, '--lint-only', '-Wno-fatal', '--top-module', module,
                 *ordered, generated / (module + '.sv')], 'lint-' + module)
        modules = [generated / (module + '.sv') for module in
                   ('SizeofImports', 'SizeofTypeOnly', 'SizeofTemplateOnly', 'SizeofExprOnly')]
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'SizeofImports', '--Mdir', work / 'obj',
             '-CFLAGS', '-std=c++23 -DSIZEOF_IMPORTS_VERILATOR -I' + str(include),
             *ordered, *modules, runner], 'verilator')
        executable = work / 'obj/VSizeofImports'
    print(run([executable], 'run'), end='')


if __name__ == '__main__':
    main()
