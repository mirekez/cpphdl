import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixture = Path(__file__).resolve().parent / 'CppGraphPackedProxyRead.cc'
    with tempfile.TemporaryDirectory(prefix='packed-proxy-read-', dir=args.work) as directory:
        work = Path(directory)

        def run(command, rejected=False):
            result = subprocess.run([str(item) for item in command], cwd=work,
                                    capture_output=True, text=True, timeout=180)
            logs = ''.join(path.read_text()[-4000:] for path in work.rglob('*.log'))
            if rejected:
                assert result.returncode and 'captured packed-array proxy mutation unsupported' in logs
                print('captured proxy mutation rejected', flush=True)
                return
            if result.returncode:
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end='', flush=True)

        flags = ['-std=c++23', '-O1', '-I' + str(fixture.parents[2] / 'include'),
                 '-fsanitize=address,undefined']
        for alias in (False, True):
            definitions = ['-DPROXY_ALIAS_TEST'] if alias else []
            run([args.cxx, *flags, *definitions, '-DPACKED_PROXY_ORDINARY', fixture,
                 '-o', work / 'ordinary'])
            run([work / 'ordinary'])
            for chunk in (0, 2):
                output = work / ('graph-' + str(alias) + '-' + str(chunk))
                frontend = ['--frontend-flag=-DPROXY_ALIAS_TEST'] if alias else []
                run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                     *frontend, '--chunk-size=' + str(chunk), '--output', output, '--runner', fixture,
                     fixture, '--', *flags, *definitions, '-DPACKED_PROXY_GRAPH'], rejected=alias)
                if not alias:
                    run([output / 'run'])


if __name__ == '__main__':
    main()
