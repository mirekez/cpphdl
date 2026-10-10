"""Lock down 1-4 lane scheduling and report comparable native-graph throughput."""
import argparse
import os
from pathlib import Path
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--workload', choices=('arithmetic', 'ram'), required=True)
    parser.add_argument('--minimum-speedup', type=float,
                        default=float(os.environ.get('CPPHDL_NATIVE_SCALING_MIN_SPEEDUP', '0')))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = Path(__file__).with_name('NativeScaling.cc')
    args.work.mkdir(parents=True, exist_ok=True)

    def run(command):
        result = subprocess.run(list(map(str, command)), text=True, capture_output=True, timeout=240)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout

    with tempfile.TemporaryDirectory(prefix=args.workload + '-', dir=args.work) as directory:
        work = Path(directory)
        flags = [args.cxx, '-std=c++23', '-I' + str(root / 'include'),
                 '-DSCALING_MEMORY=' + str(int(args.workload == 'ram'))]
        run([*flags, '-O1', '-DSCALING_EMIT', source, '-o', work / 'emit'])
        run([work / 'emit', work])
        run([*flags, '-O2', '-pthread', '-I' + str(work), source, '-o', work / 'check'])
        output = run([work / 'check'])
        print(output, end='')
        samples = {lanes: [] for lanes in range(1, 5)}
        checksums = set()
        for line in output.splitlines():
            if not line.startswith('measurement '):
                continue
            _, sample, lanes, cycles, seconds, checksum = line.split()
            samples[int(lanes)].append(int(cycles) / float(seconds))
            checksums.add(checksum)
        assert len(checksums) == 1, 'timed executions diverged across worker counts'
        assert all(len(values) == 5 for values in samples.values()), 'incomplete scaling measurements'
        baseline = statistics.median(samples[1])
        print(f'{args.workload}: lanes | cycles/s | speedup')
        for lanes, values in samples.items():
            rate = statistics.median(values)
            speedup = rate / baseline
            print(f'{lanes:>3} | {rate:>10.0f} | {speedup:.3f}x')
            # Timing floors are opt-in for dedicated performance runners: VM
            # quotas and competing host jobs cannot be inferred from lane count.
            if lanes > 1 and args.minimum_speedup:
                assert speedup >= args.minimum_speedup, (args.workload, lanes, speedup)


if __name__ == '__main__':
    main()
