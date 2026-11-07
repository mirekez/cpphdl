"""Check executable ISA-note stripping, including the HLS configure-time probe."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--readelf', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]

    def run(command, cwd=None, success=True):
        result = subprocess.run(list(map(str, command)), cwd=cwd, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=120)
        if success:
            assert result.returncode == 0, f'{command}\n{result.stdout}'
        else:
            assert result.returncode != 0, f'Unexpected success: {command}'
        return result.stdout

    def notes(path):
        return run([args.readelf, '--notes', path])

    with tempfile.TemporaryDirectory(prefix='cpphdl-isa-note-') as directory:
        work = Path(directory)
        source = work / 'source with spaces'
        source.mkdir()
        # Require v3 in metadata without emitting any v3 instructions. The test
        # is safe on baseline hosts and must remove the note before execution.
        marker = r'''
asm(R"(
.pushsection .note.gnu.property,"a",@note
.p2align 3
.long 4, 16, 5
.asciz "GNU"
.long 0xc0008002, 4, 7, 0
.popsection
)");
'''
        probe = source / 'Libcxx.cpp'
        probe.write_text(marker + (root / 'hls/tests/Libcxx.cpp').read_text())
        plain = source / 'plain.cpp'
        plain.write_text(marker + 'int main() { return 0; }\n')
        raw = work / 'unstripped'
        run([args.cxx, plain, '-o', raw])
        assert 'x86-64-v3' in notes(raw), notes(raw)
        (source / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.20)
project(GnuPropertyProbe LANGUAGES CXX)
include("${CPPHDL_ROOT}/cmake/CpphdlTestToolchain.cmake")
include("${CPPHDL_ROOT}/hls/tests/Libcxx.cmake")
if(NOT CPPHDL_OBJCOPY)
    message(FATAL_ERROR "This regression requires objcopy")
endif()
add_custom_target(hls_tests)
add_hls_native(hls_probe Libcxx.cpp)
add_executable(post_link plain.cpp)
cpphdl_strip_gnu_property_after_build(post_link)
''')

        def configure(mode, success=True):
            build = work / mode
            output = run([args.cmake, '-S', source, '-B', build,
                          '-G', 'Unix Makefiles', f'-DCPPHDL_ROOT={root}',
                          f'-DCMAKE_CXX_COMPILER={args.cxx}', f'-DHLS_CXX={args.cxx}',
                          f'-DCPPHDL_STATIC_TESTS={mode}'], success=success)
            return build, output

        for mode in ('ON', 'OFF'):
            build, _ = configure(mode)
            binary = build / 'libcxx-check'
            assert '.note.gnu.property' not in notes(binary), notes(binary)
            run([binary])
            run([args.cmake, '--build', build, '--target', 'post_link'])
            binary = build / 'post_link'
            assert '.note.gnu.property' not in notes(binary), notes(binary)
            run([binary])
            if mode == 'OFF':
                continue
            run([args.cmake, '--build', build, '--target', 'hls_probe'])
            native = build / 'native/hls_probe'
            assert '.note.gnu.property' not in notes(native), notes(native)
            run([native])
            wrapper = build / 'test-toolchain/hls-cxx'
            for flags, name in (([], 'a.out'), (['-o', 'named output'], 'named output'),
                                (['-ojoined'], 'joined'), (['--output=long'], 'long')):
                run([wrapper, plain, *flags], cwd=work)
                binary = work / name
                assert '.note.gnu.property' not in notes(binary), notes(binary)
                run([binary])
            # Object, shared-library and dry-run commands must not be stripped.
            for flags, name in ((['-c'], 'plain.o'), (['-shared', '-fPIC'], 'shared.so')):
                binary = work / name
                run([wrapper, plain, *flags, '-o', binary])
                assert 'x86-64-v3' in notes(binary), notes(binary)
            previous = raw.read_bytes()
            (work / 'joined').write_bytes(previous)
            run([wrapper, plain, '-###', '-ojoined'], cwd=work)
            assert (work / 'joined').read_bytes() == previous
            run([wrapper, source / 'missing.cpp', '-ojoined'], cwd=work, success=False)
            assert (work / 'joined').read_bytes() == previous
            header = work / 'probe.h'
            header.write_text('struct Value { int field; };\n')
            run([wrapper, '-stdlib=libc++', '-x', 'c++-header', header,
                 '-o', work / 'probe.pch'])
            assert run([wrapper, '--version']) == run([args.cxx, '--version'])

        # The configure error must retain the executable's actual diagnostic.
        probe.write_text('#include <cstdio>\nint main() {\n'
                         'std::fputs("forced runtime diagnostic\\n", stderr); return 127; }\n')
        _, output = configure('OFF', success=False)
        assert 'HLS libc++ runtime check failed: 127' in output, output
        assert 'forced runtime diagnostic' in output, output
    print('PASS: HLS probes, executable links, non-link commands and runtime diagnostics')


if __name__ == '__main__':
    main()
