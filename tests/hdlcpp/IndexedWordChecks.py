import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    for name in ('hdlcpp', 'cpphdl', 'cxx', 'verilator'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='indexed-word-', dir=args.work) as directory:
        work = Path(directory)

        def run(command):
            result = subprocess.run(list(map(str, command)), cwd=work, capture_output=True,
                                    text=True, timeout=240)
            if result.returncode:
                logs = ''.join(path.read_text(errors='replace')[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            return result.stdout

        rtl = fixtures / 'IndexedWord.sv'
        runner = fixtures / 'IndexedWordRun.cc'
        run([args.hdlcpp, rtl])
        for width in (9, 32, 64, 65, 128, 256):
            flags = ['-std=c++23', '-O2', '-I' + str(fixtures.parents[1] / 'include'),
                     '-I' + str(work), '-DINDEX_WIDTH=' + str(width)]
            ordinary = work / ('ordinary-' + str(width))
            run([args.cxx, *flags, '-fsanitize=address,undefined', runner, '-o', ordinary])
            print(run([ordinary]), end='', flush=True)
            output = work / ('verilator-' + str(width))
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'IndexedWord', '-GWIDTH=' + str(width), '--Mdir', output,
                 '-CFLAGS', ' '.join(flags + ['-DINDEX_RTL']), '-MAKEFLAGS', 'CXX=' + args.cxx,
                 rtl, runner])
            print(run([output / 'VIndexedWord']), end='', flush=True)
            seed = work / 'seed.cc'
            seed.write_text('#include "generated/IndexedWord.h"\nextern IndexedWord<' + str(width) + '> cpphdl_top;\n')
            for chunk in (0, 2):
                output = work / f'graph-{width}-{chunk}'
                run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top',
                     '--chunk-size=' + str(chunk), *(['--split-chunks=4'] if chunk else []),
                     '--cxx', args.cxx, '--frontend-flag=-I' + str(work), '--output', output,
                     '--runner', runner, seed, '--', *flags, '-DINDEX_GRAPH', '-fsanitize=address,undefined'])
                print(run([output / 'run']), end='', flush=True)
        print('indexed word: reads, masked writes, temporary/named storage, invalid indices and odd widths PASS')


if __name__ == '__main__':
    main()
