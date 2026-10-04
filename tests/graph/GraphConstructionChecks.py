import argparse
from pathlib import Path
import resource
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent
    include = fixtures.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)

    def limit_memory():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        limit = 96 * 1024**2
        resource.setrlimit(resource.RLIMIT_AS, (limit, limit))

    with tempfile.TemporaryDirectory(prefix='graph-construction-', dir=args.work) as directory:
        work = Path(directory)

        def run(command, limited=False):
            result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                    capture_output=True, timeout=120,
                                    preexec_fn=limit_memory if limited else None)
            if result.returncode:
                raise AssertionError((command, result.returncode, result.stdout, result.stderr))
            print(result.stdout, end='', flush=True)

        flags = ['-std=c++23', '-O2', '-I' + str(include)]
        run([args.cxx, *flags, fixtures / 'GraphConstruction.cc', '-o', work / 'construct'])
        run([work / 'construct', 'storage', 262145], limited=True)
        run([work / 'construct', 'folds', 65536], limited=True)
        run([args.cxx, *flags, '-fsanitize=address,undefined',
             fixtures / 'GraphConstruction.cc', '-o', work / 'construct-sanitized'])
        run([work / 'construct-sanitized', 'folds', 4096])
        run([work / 'construct-sanitized', 'emit', work / 'graph.cc'])
        run([args.cxx, *flags, '-fsanitize=address,undefined', work / 'graph.cc', '-o', work / 'emit'])
        run([work / 'emit', work / 'model.h'])
        run([args.cxx, *flags, '-fsanitize=address,undefined', '-I' + str(work),
             fixtures / 'GraphConstructionRun.cc', '-o', work / 'run'])
        run([work / 'run'])


if __name__ == '__main__':
    main()
