"""Compiler discovery must agree with the RISC-V CTest launcher."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tests'))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='toolchain-', dir=args.work) as directory:
        work = Path(directory)
        toolchain = TestToolchain(args.cxx, None, work)
        source = work / 'probe.cpp'
        source.write_text('#include "RiscvToolchain.h"\n#include <iostream>\n'
                          'int main() { auto tool = tribe_test::find_riscv_tool("riscv32-unknown-elf-gcc");'
                          'if (tool.empty()) return 1; std::cout << tool.string(); }\n')
        probe = work / 'probe'
        subprocess.run([args.cxx, '-std=c++17', '-I' + str(Path(__file__).resolve().parents[1]),
                        str(source), '-o', str(probe)], env=toolchain.env, check=True, timeout=60)

        def compiler(directory, executable=True):
            directory.mkdir(parents=True, exist_ok=True)
            tool = directory / 'riscv32-unknown-elf-gcc'
            tool.write_text('#!/bin/sh\nexit 0\n')
            tool.chmod(0o755 if executable else 0o644)
            return tool

        root = compiler(work / 'preferred' / 'bin')
        alternate = compiler(work / 'alternate' / 'bin')
        path = compiler(work / 'path with spaces')
        home = compiler(work / 'home' / 'riscv' / 'bin')
        local = compiler(work)
        blocked = compiler(work / 'not-executable', executable=False)
        missing = str(work / 'missing')
        cases = [
            ({'RISCV_HOME': str(root.parent.parent), 'RISCV': str(alternate.parent.parent),
              'PATH': str(path.parent)}, root),
            ({'RISCV_HOME': missing, 'RISCV': str(alternate.parent.parent)}, alternate),
            ({'PATH': str(path.parent)}, path),
            ({'RISCV_HOME': missing, 'RISCV': missing, 'PATH': str(path.parent)}, path),
            ({'PATH': str(blocked.parent) + ':' + str(path.parent)}, path),
            ({'PATH': ''}, Path('.') / local.name),
            ({'PATH': missing + ':'}, Path('.') / local.name),
            ({}, home),
            ({'HOME': missing}, None),
        ]
        for changes, expected in cases:
            env = toolchain.env.copy()
            env.pop('RISCV_HOME', None)
            env.pop('RISCV', None)
            env.update({'HOME': str(home.parents[2]), 'PATH': missing})
            env.update(changes)
            result = subprocess.run([str(probe)], env=env, cwd=work, text=True,
                                    capture_output=True, timeout=10)
            assert result.returncode == (0 if expected else 1), (changes, result.stderr)
            if expected:
                assert Path(result.stdout) == expected, (changes, result.stdout, expected)
        print('RISC-V compiler discovery: 9 PATH/environment/fallback cases PASS')


if __name__ == '__main__':
    main()
