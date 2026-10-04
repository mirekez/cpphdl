"""Compare dynamic slice writes in C++, native graph, and generated RTL."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='wide-write-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            logs = '\n'.join(p.read_text()[-3000:] for p in work.glob('*/cpp-to-graph.log'))
            raise RuntimeError(f'{label}: {result.stdout[-5000:]}\n{logs}\nArtifacts: {work}')
        return result.stdout

    source = fixture / 'WideWrite.cc'
    runner = fixture / 'WideWriteRun.cc'
    if args.verilator:
        generated = work / 'rtl'
        run([args.cpphdl, '--generated-dir=' + str(generated), source,
             '--', '-std=c++23', '-I' + str(include)], 'convert')
    for line, width, data, aligned in ((128, 64, 64, 1), (128, 64, 64, 0),
                                      (65, 1, 64, 0), (193, 96, 128, 0),
                                      (193, 96, 32, 0), (64, 32, 64, 0), (32, 8, 32, 0)):
        name = f'{line}-{width}-{data}-{aligned}'
        output = work / name
        defines = [f'-DWRITE_LINE={line}', f'-DWRITE_SLICE={width}',
                   f'-DWRITE_DATA={data}', f'-DWRITE_ALIGNED={aligned}']
        common = ['-std=c++23', *defines, '-I' + str(include)]
        if args.verilator:
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'WideWrite', '--Mdir', output,
                 f'-GLineWidth={line}', f'-GSliceWidth={width}', f'-GDataWidth={data}', f'-GAligned={aligned}',
                 '-CFLAGS', ' '.join([*common, '-DWRITE_RTL']),
                 generated / 'Predef_pkg.sv', generated / 'WideWrite.sv', runner], name + '-rtl-build')
            executable = output / 'VWideWrite'
        else:
            native = work / (name + '-native')
            run([args.cxx, *common, '-O1', '-fsanitize=address,undefined', runner, '-o', native], name + '-native-build')
            print(run([native], name + '-native-run'), end='')
            native.unlink()  # Successful sanitizer binaries need not accumulate.
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 *['--frontend-flag=' + flag for flag in defines], '--output', output,
                 '--runner', runner, source, '--', *common, '-DWRITE_GRAPH',
                 '-fsanitize=address,undefined'], name + '-graph')
            executable = output / 'run'
        print(run([executable], name + '-run'), end='')
        if not args.verilator:
            executable.unlink()


if __name__ == '__main__':
    main()
