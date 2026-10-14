"""Build a configurable, self-checking RV32 matmul ELF without simulator regeneration."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cva6-source', type=Path, required=True)
    parser.add_argument('--riscv', type=Path, default=Path('/home/me/riscv'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--size', type=int, default=32)
    parser.add_argument('--repeats', type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.size <= 128 or not 1 <= args.repeats <= 1024:
        parser.error('use size 1..128 and repeats 1..1024')
    output = args.output.resolve()
    if not output.is_relative_to(Path('/tmp')):
        parser.error('build artifacts must be inside /tmp')
    output.mkdir(parents=True, exist_ok=True)
    support = Path(__file__).resolve().parent
    source = args.cva6_source.resolve()
    common = source / 'verif/tests/custom/common'
    crt = common / 'crt.S'
    linker = source / 'config/gen_from_riscv_config/linker/link.ld'
    workload = support / 'matrix_multiply_large.cpp'
    runtime = support / 'matrix_runtime.c'
    flags = ['-O2', '-static', '-mcmodel=medany', '-fvisibility=hidden', '-nostdlib',
             '-nostartfiles', '-ffreestanding', '-fno-builtin', '-msmall-data-limit=0',
             '-march=rv32imac_zicsr_zifencei', '-mabi=ilp32']
    includes = ['-I' + str(path) for path in [common, source / 'verif/tests/custom/env',
                                            source / 'verif/sim/dv/user_extension']]
    cxx = args.riscv.resolve() / 'bin/riscv32-unknown-elf-g++'
    cc = args.riscv.resolve() / 'bin/riscv32-unknown-elf-gcc'
    commands = [
        [cxx, *flags, *includes, '-fno-exceptions', '-fno-rtti',
         '-DMATRIX_SIZE=' + str(args.size), '-DMATRIX_REPEATS=' + str(args.repeats),
         '-c', workload, '-o', output / 'matrix.o'],
        [cc, *flags, *includes, '-c', runtime, '-o', output / 'runtime.o'],
        [cc, *flags, *includes, '-c', crt, '-o', output / 'crt.o'],
        [cxx, *flags, '-T', linker, output / 'matrix.o', output / 'runtime.o',
         output / 'crt.o', '-lgcc', '-o', output / 'matrix_multiply.riscv']]
    for command in commands:
        subprocess.run(list(map(str, command)), check=True)
    objdump = args.riscv.resolve() / 'bin/riscv32-unknown-elf-objdump'
    disassembly = subprocess.check_output([objdump, '-d', output / 'matrix_multiply.riscv'], text=True)
    (output / 'disassembly.txt').write_text(disassembly)
    with (output / 'matrix_multiply.riscv').open('rb') as stream:
        elf_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    report = dict(size=args.size, repeats=args.repeats, macs=args.size**3 * args.repeats,
                  matrix_bytes=3 * args.size**2 * 4, elf_sha256=elf_hash,
                  commands=[list(map(str, command)) for command in commands],
                  inputs={str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in [workload, runtime, crt, linker]})
    (output / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(output / 'matrix_multiply.riscv')


if __name__ == '__main__':
    main()
