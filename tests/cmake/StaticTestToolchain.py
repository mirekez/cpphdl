"""Executable links must not depend on the host's C++ runtime shared objects."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--real-cxx', required=True)
    parser.add_argument('--readelf', required=True)
    parser.add_argument('--verilator')
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--ctest', required=True)
    parser.add_argument('--build', required=True)
    parser.add_argument('--libcxx-probe')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cpphdl-static-runtime-') as directory:
        work = Path(directory)
        toolchain = TestToolchain(args.cxx, args.verilator, work)
        env = toolchain.env.copy()
        env.pop('LD_LIBRARY_PATH', None)

        def run(command):
            result = subprocess.run(list(map(str, command)), env=env, text=True,
                                    capture_output=True, timeout=120)
            if result.returncode:
                raise RuntimeError(f'{command}:\n{result.stdout}{result.stderr}')
            return result.stdout

        def check(binary):
            notes = run([args.readelf, '--notes', binary])
            assert '.note.gnu.property' not in notes, notes
            dynamic = run([args.readelf, '--dynamic', binary])
            needed = '\n'.join(line for line in dynamic.splitlines() if '(NEEDED)' in line)
            assert not re.search(r'lib(?:stdc\+\+|c\+\+(?:abi)?|gcc_s)\.', needed), needed
            run([binary])

        headers = run([args.readelf, '--program-headers', '--dynamic', args.cpphdl])
        assert 'INTERP' not in headers and '(NEEDED)' not in headers, headers
        run([args.cpphdl, '--help'])
        if args.libcxx_probe:
            check(args.libcxx_probe)
        # Catch registrations accidentally reverting to the shared converter
        # or bypassing the runtime wrapper when compiling their own runners.
        tests = json.loads(run([args.ctest, '--test-dir', args.build, '--show-only=json-v1']))['tests']
        converter_calls = 0
        for test in tests:
            command = test.get('command', [])
            for index, arg in enumerate(command):
                assert arg != str(Path(args.cpphdl).with_name('cpphdl')), (test['name'], arg)
                assert not arg.startswith(('-DCPPHDL=', '-DCONVERTER=')) or arg.split('=', 1)[1] != str(
                    Path(args.cpphdl).with_name('cpphdl')), (test['name'], arg)
                if arg == '--cxx':
                    assert command[index + 1] == args.cxx, (test['name'], command)
                if arg == args.cpphdl or arg.endswith('=' + args.cpphdl):
                    converter_calls += 1
        assert converter_calls > 0, 'No tests use cpphdl-static'

        # Introspection must return the underlying toolchain's paths. Native
        # graph and HLS header discovery both rely on these queries.
        for query in ('--version', '-print-file-name=libstdc++.a'):
            assert run([args.cxx, query]) == run([args.real_cxx, query])
        # Verilator builds precompiled headers without -c. In particular,
        # libc++ ABI link libraries must not become extra compiler inputs.
        header = work / 'pch.h'
        header.write_text('struct StaticHeader { int value; };\n')
        header_flags = ['-stdlib=libc++'] if 'clang' in run([args.cxx, '--version']).lower() else []
        run([args.cxx, *header_flags, '-x', 'c++-header', header, '-o', work / 'pch.gch'])
        source = work / 'test.cc'
        source.write_text('''#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
int main() {
    std::vector<std::string> values;
    std::thread worker([&] { values.emplace_back("static runtime"); });
    worker.join();
    try { throw std::runtime_error(values.at(0)); }
    catch (const std::exception& e) { return std::string(e.what()) != "static runtime"; }
}
''')
        obj = work / 'test.o'
        run([args.cxx, '-std=c++17', '-c', source, '-o', obj])
        binary = work / 'native'
        run([args.cxx, obj, '-pthread', '-o', binary])
        check(binary)
        run([args.cxx, '-std=c++17', '-pthread', source,
             *toolchain.sanitizers(), '-o', work / 'sanitized'])
        check(work / 'sanitized')
        # Shared-library fixtures must remain buildable; they intentionally
        # test dynamic loading rather than deployment of a standalone binary.
        run([args.cxx, '-std=c++17', '-shared', '-fPIC', source, '-pthread',
             '-o', work / 'fixture.so'])
        if args.verilator:
            rtl = work / 'StaticRuntime.sv'
            rtl.write_text('module StaticRuntime(input logic a, output logic y); assign y = a; endmodule\n')
            runner = work / 'runner.cpp'
            runner.write_text('#include "VStaticRuntime.h"\n'
                              'int main() { VStaticRuntime m; m.a = 1; m.eval(); return m.y != 1; }\n')
            command = toolchain.command([args.verilator, '--cc', '--exe', '--build', '-j', '1',
                                         '--top-module', 'StaticRuntime', '--Mdir', work / 'obj',
                                         rtl, runner])
            run(command)
            check(work / 'obj' / 'VStaticRuntime')
        print('Static C++ runtimes: compiler queries, native, sanitizers, shared fixture and Verilator PASS')


if __name__ == '__main__':
    main()
