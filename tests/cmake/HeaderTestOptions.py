"""Check header self-test options and migration of existing CMake caches."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='cpphdl-header-options-') as directory:
        source = Path(directory)
        (source / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.15)
project(HeaderTestOptions NONE)
include("${CPPHDL_ROOT}/cmake/CpphdlTestOptions.cmake")
foreach(component LIB EXAMPLES)
    if(DEFINED CPPHDL_BUILD_${component})
        message(FATAL_ERROR "Deprecated option remains defined")
    endif()
endforeach()
''')

        def configure(name, expected, *options):
            build = source / name
            result = subprocess.run([args.cmake, '-S', str(source), '-B', str(build),
                                     '-G', 'Unix Makefiles', f'-DCPPHDL_ROOT={root}',
                                     *options], text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=30)
            assert result.returncode == 0, result.stdout
            cache = dict(line.split('=', 1) for line in
                         (build / 'CMakeCache.txt').read_text().splitlines()
                         if line.startswith('CPPHDL_BUILD_'))
            for component, value in zip(('LIB', 'EXAMPLES'), expected):
                assert cache[f'CPPHDL_BUILD_{component}_TESTS:BOOL'] == value, cache
                assert not any(key.split(':')[0] == f'CPPHDL_BUILD_{component}'
                               for key in cache), cache
            return result.stdout

        configure('defaults', ('ON', 'ON'))
        configure('independent', ('OFF', 'ON'), '-DCPPHDL_BUILD_LIB_TESTS=OFF')
        configure('independent', ('ON', 'OFF'), '-DCPPHDL_BUILD_LIB_TESTS=ON',
                  '-DCPPHDL_BUILD_EXAMPLES_TESTS=OFF')
        configure('off', ('OFF', 'OFF'), '-DCPPHDL_BUILD_LIB_TESTS=OFF',
                  '-DCPPHDL_BUILD_EXAMPLES_TESTS=OFF')
        output = configure('legacy', ('OFF', 'ON'), '-DCPPHDL_BUILD_LIB=OFF',
                           '-DCPPHDL_BUILD_EXAMPLES=ON')
        assert 'was renamed to CPPHDL_BUILD_LIB_TESTS' in output, output
        assert 'was renamed to CPPHDL_BUILD_EXAMPLES_TESTS' in output, output
        output = configure('legacy', ('OFF', 'ON'))
        assert 'was renamed' not in output, output
        configure('precedence', ('ON', 'OFF'), '-DCPPHDL_BUILD_LIB=OFF',
                  '-DCPPHDL_BUILD_LIB_TESTS=ON', '-DCPPHDL_BUILD_EXAMPLES=ON',
                  '-DCPPHDL_BUILD_EXAMPLES_TESTS=OFF')
        # Once migrated, an old flag must not override an existing new setting.
        configure('precedence', ('ON', 'OFF'), '-DCPPHDL_BUILD_LIB=OFF',
                  '-DCPPHDL_BUILD_EXAMPLES=ON')
    print('PASS: header test defaults, independent switches and cache migration')


if __name__ == '__main__':
    main()
