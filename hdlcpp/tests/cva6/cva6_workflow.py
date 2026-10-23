"""Shared implementation of the CVA6 build/run shell entry points."""

import argparse
from contextlib import ExitStack
import ctypes
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import signal
import subprocess
import sys
import time

from compare_matmul import parse_run, simulation_command


SUPPORT = Path(__file__).resolve().parent
REPO = SUPPORT.parents[2]
MODES = ('verilator', 'cpphdl', 'cpphdl-optimize-combs',
         'cpphdl-native-graph', 'cpphdl-native-graph-4threads')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def remove(path, root):
    if not path.is_relative_to(root) or path == root or path.is_symlink():
        raise ValueError(f'refusing unsafe cleanup: {path}')
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def executable(value):
    path = shutil.which(str(value))
    if path is None:
        raise RuntimeError(f'missing executable: {value}')
    return str(Path(path).absolute())


def quota_remaining(path):
    class Quota(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint64) for name in
                    ('hard', 'soft', 'space', 'ihard', 'isoft', 'inodes', 'btime', 'itime')]
        _fields_.append(('valid', ctypes.c_uint32))

    libc = ctypes.CDLL(None, use_errno=True)
    query = getattr(libc, 'quotactl_fd', None)
    if query is None:
        if platform.machine() not in ('x86_64', 'aarch64', 'riscv64'):
            return None
        query = lambda *arguments: libc.syscall(443, *arguments)
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        quota = Quota()
        if query(descriptor, ctypes.c_uint(0x800007 << 8), os.getuid(), ctypes.byref(quota)):
            return None
        limits = [limit * 1024 for limit in (quota.hard, quota.soft) if limit]
        return max(0, min(limits) - quota.space) if limits and quota.valid & 1 else None
    finally:
        os.close(descriptor)


def temporary_free(path):
    free = shutil.disk_usage(path).free
    try:
        remaining = quota_remaining(path)
    except (OSError, AttributeError):
        remaining = None
    # tmpfs user quotas can reject writes while df still reports ample space.
    return min(free, remaining) if remaining is not None else free


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_AS, (6_000_000_000, 6_000_000_000))
    resource.setrlimit(resource.RLIMIT_FSIZE, (768 * 1024**2, 768 * 1024**2))
    resource.setrlimit(resource.RLIMIT_STACK, (256 * 1024**2, 256 * 1024**2))


def stop_process(process):
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def checked(command, directory, log, timeout=3600, environment=None):
    command = list(map(str, command))
    log.parent.mkdir(parents=True, exist_ok=True)
    if temporary_free(log.parent) < 512 * 1024**2:
        raise RuntimeError('less than 512 MiB temporary space/user quota available before stage')
    started = time.monotonic()
    peak = 0
    reason = None
    print('+ ' + ' '.join(command), flush=True)
    with ExitStack() as stack:
        output = stack.enter_context(log.open('w'))
        process = subprocess.Popen(command, cwd=directory, env=environment,
                                   stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True, preexec_fn=limits)
        stack.callback(stop_process, process)
        while process.poll() is None:
            available = int(next(line.split()[1] for line in Path('/proc/meminfo').read_text().splitlines()
                                 if line.startswith('MemAvailable:'))) * 1024
            resident = 0
            for status in Path('/proc').glob('[0-9]*/stat'):
                try:
                    fields = status.read_text().rsplit(')', 1)[1].split()
                    if int(fields[2]) == process.pid:
                        resident += int(fields[21]) * os.sysconf('SC_PAGE_SIZE')
                except (OSError, ValueError, IndexError):
                    pass
            peak = max(peak, resident)
            if available < 600 * 1024**2:
                reason = 'available RAM below 600 MiB'
            elif temporary_free(log.parent) < 512 * 1024**2:
                reason = 'less than 512 MiB free temporary space/user quota'
            elif time.monotonic() - started > timeout:
                reason = 'stage timeout'
            if reason:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                break
            time.sleep(0.5)
        status = process.wait()
    report = dict(command=command, status=status, reason=reason,
                  seconds=time.monotonic() - started, peak_rss_bytes=peak)
    save(log.with_suffix('.json'), report)
    if status or reason:
        with log.open('rb') as stream:
            stream.seek(max(0, log.stat().st_size - 4000))
            tail = stream.read().decode(errors='replace')
        raise RuntimeError(f'{reason or "command failed"}; see {log}\n{tail}')
    return report


class Workflow:
    def __init__(self, args):
        self.args = args
        self.root = args.work_root.resolve()
        if self.root == Path('/tmp') or not self.root.is_relative_to(Path('/tmp')):
            raise ValueError('CVA6_WORK_ROOT must be a subdirectory of /tmp')
        self.source = self.root / 'source'
        self.ordinary = self.root / 'ordinary'
        self.riscv = Path(os.environ.get('RISCV', str(Path.home() / 'riscv'))).resolve()
        self.original = Path(os.environ.get('CVA6_SRC', str(REPO.parent / 'cva6'))).resolve()
        self.cxx = executable(os.environ.get('CPPHDL_CXX', str(REPO / '.conda/bin/clang++')))
        self.cpphdl = os.environ.get('CPPHDL', str(REPO / 'build/cpphdl'))
        self.hdlcpp = os.environ.get('HDLCPP', str(REPO / 'hdlcpp/build/hdlcpp'))
        default_verilator = self.original / 'tools/verilator-new/bin/verilator'
        self.verilator = os.environ.get('VERILATOR', str(default_verilator) if default_verilator.exists() else 'verilator')
        available = sorted(os.sched_getaffinity(0))
        self.cpu = args.cpu if args.cpu is not None else (2 if 2 in available else available[0])
        if self.cpu not in available:
            raise ValueError(f'CPU {self.cpu} is not in this process affinity mask')
        self.cpus4 = args.cpus4 or ','.join(map(str, (available[available.index(self.cpu):] + available)[:4]))
        self.elf = Path(os.environ.get('ELF', str(self.root / 'elf/matrix_multiply.riscv'))).resolve()

    def freeze_tool(self, value, name):
        original = Path(executable(value))
        target = self.root / 'tools' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or digest(target) != digest(original):
            shutil.copy2(original, target)
        return target

    def run_command(self, command, label, directory=None, environment=None):
        return checked(command, directory or self.root, self.root / 'logs' / (label + '.log'),
                       self.args.timeout, environment)

    def require(self, path, instruction):
        if not path.is_file():
            raise RuntimeError(f'missing {path}; run {instruction} first')

    def snapshot_intact(self, record):
        return all((self.source / name).is_file() and digest(self.source / name) == expected
                   for name, expected in record['files'].items())

    def signature(self, mode):
        if not self.snapshot_intact(json.loads((self.root / 'source.json').read_text())):
            raise RuntimeError('source snapshot changed; rerun .build_cva6.sh and .build_cva6_hdlcpp.sh')
        paths = [SUPPORT / 'cva6_workflow.py', self.root / 'source.json',
                 Path(self.cxx), self.riscv / 'lib/libfesvr.a',
                 *(self.riscv / 'include').rglob('*.h'), *(REPO / 'include').rglob('*.h')]
        if mode == 'verilator':
            driver = Path(executable(self.verilator))
            paths += [driver, driver.with_name('verilator_bin'),
                      *(driver.parent.parent / 'include').rglob('*.h')]
        else:
            self.require(self.root / 'ordinary.json', '.build_cva6_hdlcpp.sh')
            ordinary = json.loads((self.root / 'ordinary.json').read_text())
            if ordinary['source_sha256'] != digest(self.root / 'source.json'):
                raise RuntimeError('source snapshot changed; rerun .build_cva6_hdlcpp.sh')
            if any(not (self.ordinary / name).is_file() or digest(self.ordinary / name) != expected
                   for name, expected in ordinary['files'].items()):
                raise RuntimeError('ordinary C++ conversion changed; rerun .build_cva6_hdlcpp.sh')
            paths += [Path(executable(self.cpphdl)), Path(executable(self.hdlcpp)),
                      self.ordinary / 'all_generated.h', *(self.ordinary / 'generated').rglob('*.h')]
            if 'native-graph' in mode:
                paths += [SUPPORT / name for name in ('build_native_graph.py', 'native_graph_top.cpp',
                          'run_native_graph_model.cpp')]
                paths += [self.ordinary / 'run_cpphdl_testharness.cpp', self.ordinary / 'run_cpphdl_testharness_model.h',
                          REPO / 'tools/cpphdl-graph.py', REPO / 'tools/cpphdl-graph-emit.cpp']
            else:
                paths += [SUPPORT / 'build_cpp_model.py',
                          *(SUPPORT / 'support/cpphdl').glob('run_cpphdl_testharness*')]
                if mode.endswith('optimize-combs'):
                    paths += [SUPPORT / 'support/cpphdl' / name for name in
                              ('prepare_optimize_combs.py', 'run_optimize_combs.sh')]
        values = {str(path): digest(path) for path in sorted(set(paths)) if path.is_file()}
        values.update(mode=mode, cxx=self.cxx, riscv=str(self.riscv), target='cv32a6_imac_sv32')
        return hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()

    def prepare(self):
        if os.environ.get('TARGET', 'cv32a6_imac_sv32') != 'cv32a6_imac_sv32':
            raise RuntimeError('this reproducible benchmark requires TARGET=cv32a6_imac_sv32')
        self.require(self.original / 'Makefile', 'set CVA6_SRC to an initialized CVA6 checkout')
        files = [self.original / 'Makefile', self.original / 'verilator_config.vlt']
        ignored = {'.git', '__pycache__', 'build', 'obj_dir', 'work', 'node_modules'}
        suffixes = {'.sv', '.v', '.svh', '.vh', '.vhd', '.h', '.hpp', '.c', '.cc', '.cpp',
                    '.S', '.ld', '.py', '.sh', '.f', '.Flist', '.flist', '.mk', '.yml', '.yaml', '.json', '.txt', '.list', '.vlt'}
        for name in ('core', 'corev_apu', 'config', 'common', 'vendor', 'util', 'verif'):
            for directory, folders, names in os.walk(self.original / name):
                folders[:] = [folder for folder in folders if folder not in ignored and not folder.startswith('out_')]
                for filename in names:
                    path = Path(directory) / filename
                    if path.is_file() and (path.suffix in suffixes or filename.startswith(('Makefile', 'Flist.'))):
                        files.append(path)
        hashes = {str(path.relative_to(self.original)): digest(path) for path in sorted(set(files))}
        record = dict(origin=str(self.original), target='cv32a6_imac_sv32', files=hashes)
        stamp = self.root / 'source.json'
        if (self.args.force or not stamp.exists() or json.loads(stamp.read_text()) != record
                or not self.snapshot_intact(record)):
            remove(self.source, self.root)
            for relative in hashes:
                destination = self.source / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(self.original / relative, destination)
            save(stamp, record)
        self.run_command([sys.executable, '-B', SUPPORT / 'build_matrix_benchmark.py',
                          '--cva6-source', self.source, '--riscv', self.riscv,
                          '--output', self.root / 'elf'], 'elf')
        print(f'CVA6 source and 32x32 matmul prepared in {self.root}', flush=True)

    def convert(self):
        self.require(self.root / 'source.json', '.build_cva6.sh')
        tool = str(self.freeze_tool(self.hdlcpp, 'hdlcpp'))
        inputs = dict(source_sha256=digest(self.root / 'source.json'), hdlcpp_sha256=digest(tool),
                      support={str(path.relative_to(SUPPORT)): digest(path)
                               for path in [SUPPORT / 'convert.sh', *(SUPPORT / 'support/cpphdl').rglob('*')]
                               if path.is_file() and (path.name == 'convert.sh' or path.suffix == '.tsv'
                                  or path.name.startswith('cva6_') or path.parent.name == 'tools')})
        stamp = self.root / 'ordinary.json'
        if not self.args.force and stamp.exists():
            prior = json.loads(stamp.read_text())
            same_inputs = (all(prior.get(key) == inputs[key] for key in ('source_sha256', 'hdlcpp_sha256'))
                           and all(prior.get('support', {}).get(key) == value
                                   for key, value in inputs['support'].items()))
            if same_inputs and all(
                    (self.ordinary / name).is_file() and digest(self.ordinary / name) == value
                    for name, value in prior['files'].items()):
                for path in (SUPPORT / 'support/cpphdl').glob('run_cpphdl_testharness*'):
                    if path.is_file():
                        shutil.copy2(path, self.ordinary / path.name)
                inputs.update(seconds=prior['seconds'], files={str(path.relative_to(self.ordinary)): digest(path)
                              for path in self.ordinary.rglob('*') if path.is_file()})
                save(stamp, inputs)
                print('ordinary conversion: verified cached result', flush=True)
                return
        stamp.unlink(missing_ok=True)
        environment = dict(os.environ, CVA6_SRC=str(self.source), CPPHDL_OUT=str(self.ordinary),
                           HDLCPP=tool, RISCV=str(self.riscv), TARGET='cv32a6_imac_sv32',
                           CPPHDL_CVA6_NATIVE_HARNESS='1', CPPHDL_CVA6_SKIP_OPTIMIZE='1',
                           HDLCPP_JOBS='1', JOBS='1', PYTHONUNBUFFERED='1')
        for name in ('CPPHDL_CVA6_ONLY', 'CPPHDL_CVA6_FINALIZE_ONLY', 'CPPHDL_CVA6_RESUME_TRAITS'):
            environment.pop(name, None)
        result = self.run_command(['bash', SUPPORT / 'convert.sh'], 'conversion', environment=environment)
        inputs.update(seconds=result['seconds'], files={str(path.relative_to(self.ordinary)): digest(path)
                       for path in self.ordinary.rglob('*') if path.is_file()})
        save(stamp, inputs)

    def build(self, mode):
        self.require(self.root / 'source.json', '.build_cva6.sh')
        signature = self.signature(mode)
        stamp = self.root / 'builds' / (mode + '.json')
        output = self.root / mode
        binary = output / ('Variane_testharness' if mode == 'verilator' else 'run')
        if stamp.exists() and not self.args.force:
            prior = json.loads(stamp.read_text())
            if prior.get('signature') == signature and binary.is_file() and prior.get('binary_sha256') == digest(binary):
                print(f'{mode}: verified cached build', flush=True)
                return dict(prior, cached=True)
        stamp.unlink(missing_ok=True)
        remove(output, self.root)
        environment = dict(os.environ, RISCV=str(self.riscv), CVA6_REPO_DIR=str(self.source),
                           TARGET_CFG='cv32a6_imac_sv32', HPDCACHE_DIR=str(self.source / 'core/cache_subsystem/hpdcache'))
        if mode == 'verilator':
            command = ['make', '-C', self.source, 'verilate', 'verilator=' + executable(self.verilator),
                       'VERILATOR_INSTALL_DIR=' + str(Path(executable(self.verilator)).parent.parent),
                       'SPIKE_INSTALL_DIR=' + str(self.riscv), 'ver-library=' + str(output),
                       'target=cv32a6_imac_sv32', 'verilator_threads=1', 'NUM_JOBS=1', 'CXX=' + self.cxx,
                       'TRACE_FAST=', 'TRACE_COMPACT=', 'PROFILE=', 'DEBUG=', 'SPIKE_TANDEM=']
        elif 'native-graph' in mode:
            command = [sys.executable, '-B', SUPPORT / 'build_native_graph.py', '--ordinary', self.ordinary,
                       '--output', output, '--cpphdl', self.freeze_tool(self.cpphdl, 'cpphdl'), '--cxx', self.cxx,
                       '--riscv', self.riscv, '--threads', '4' if mode.endswith('4threads') else '1']
        else:
            command = [sys.executable, '-B', SUPPORT / 'build_cpp_model.py', '--ordinary', self.ordinary,
                       '--output', output, '--hdlcpp', self.freeze_tool(self.hdlcpp, 'hdlcpp'),
                       '--cpphdl', self.freeze_tool(self.cpphdl, 'cpphdl'),
                       '--cxx', self.cxx, '--riscv', self.riscv,
                       '--mode', 'optimize-combs' if mode.endswith('optimize-combs') else 'default',
                       '--cache-dir', self.root / 'compile-cache' / mode]
        result = self.run_command(command, 'build-' + mode, environment=environment)
        if signature != self.signature(mode):
            raise RuntimeError('build inputs changed during compilation; refusing a mixed-generation binary')
        record = dict(signature=signature, binary=str(binary), binary_sha256=digest(binary),
                      seconds=result['seconds'], peak_rss_bytes=result['peak_rss_bytes'])
        save(stamp, record)
        if not self.args.keep_build_files:
            if mode == 'verilator':
                for path in output.iterdir():
                    if path != binary:
                        remove(path, self.root)
            elif 'native-graph' in mode:
                for path in (output / 'graph').iterdir():
                    if path.suffix not in ('.json', '.log', '.tsv', '.d'):
                        remove(path, self.root)
            else:
                remove(output / 'work', self.root)
                remove(self.root / 'compile-cache' / mode, self.root)
        return record

    def ready(self, mode):
        stamp = self.root / 'builds' / (mode + '.json')
        self.require(stamp, '.build_cva6_' + mode + '.sh')
        record = json.loads(stamp.read_text())
        binary = Path(record['binary'])
        if record['signature'] != self.signature(mode) or not binary.is_file() or digest(binary) != record['binary_sha256']:
            raise RuntimeError(f'{mode}: stale build; rerun its build script')
        return binary

    def run(self, mode):
        binary = self.ready(mode)
        self.require(self.elf, '.build_cva6.sh')
        directory = self.root / 'runs' / mode
        remove(directory, self.root)
        directory.mkdir(parents=True)
        environment = {name: value for name, value in os.environ.items()
                       if not name.startswith(('CPPHDL_', 'HDLCPP_', 'CVA6_', 'NATIVE_')) and name != 'LD_PRELOAD'}
        environment['CVA6_BENCH_STATS'] = '1'
        cpus = self.cpus4 if mode.endswith('4threads') else str(self.cpu)
        command = ['taskset', '-c', cpus, *simulation_command(binary, 'verilator' if mode == 'verilator' else 'cpphdl',
                                                           self.elf, self.args.max_cycles)]
        self.run_command(command, 'run-' + mode, directory, environment)
        text = (self.root / 'logs' / ('run-' + mode + '.log')).read_text()
        result = parse_run(text, 0, require_graph_edges='native-graph' in mode)
        result.update(elf_sha256=digest(self.elf), binary_sha256=digest(binary))
        save(directory / 'result.json', result)
        if not result['passed']:
            raise RuntimeError(f'{mode}: failed output/cycle validation')
        print(text, end='')
        if not self.args.keep_traces:
            self.clean_traces(directory)

    def clean_traces(self, directory):
        for path in directory.rglob('*'):
            if path.is_file() and (path.suffix in ('.dasm', '.traces') or path.parent.name == 'validation-cpphdl' and path.name == 'run.log'):
                path.unlink()

    def compare(self):
        results = {}
        failed = False
        for mode in MODES:
            try:
                results[mode] = dict(build=self.build(mode))
            except (RuntimeError, OSError, ValueError) as error:
                results[mode] = dict(error=str(error))
                print(f'{mode}: {error}', file=sys.stderr, flush=True)
                failed = True
        for mode in MODES[1:]:
            if 'error' in results[mode] or 'error' in results['verilator']:
                continue
            output = self.root / 'comparisons' / mode
            remove(output, self.root)
            command = [sys.executable, '-B', SUPPORT / 'compare_matmul.py', '--check-commits',
                       '--verilator', self.ready('verilator'), '--cpphdl', self.ready(mode), '--elf', self.elf,
                       '--output', output, '--max-cycles', self.args.max_cycles, '--trials', self.args.trials,
                       '--cpu', self.cpu, '--timeout', self.args.timeout]
            if 'native-graph' in mode:
                command.append('--native-graph')
            if mode.endswith('4threads'):
                command += ['--cpphdl-cpus', self.cpus4]
            try:
                self.run_command(command, 'compare-' + mode)
                report = json.loads((output / 'comparison.json').read_text())
                if not report['equivalent']:
                    raise RuntimeError('comparison did not establish equivalence')
                results[mode]['comparison'] = report
            except (RuntimeError, OSError, ValueError) as error:
                results[mode]['error'] = str(error)
                failed = True
            if not self.args.keep_traces and 'comparison' in results[mode] and output.exists():
                self.clean_traces(output)
        save(self.root / 'comparison.json', dict(elf=str(self.elf), results=results, passed=not failed))
        print('\nMode                                  Build (s)    Median work (s)   Cycles    Result')
        for mode in MODES:
            value = results[mode]
            build = f"{value['build']['seconds']:.3f}" if 'build' in value else '-'
            if value.get('build', {}).get('cached'):
                build += '*'
            report = value.get('comparison')
            reference = False
            if mode == 'verilator':
                report = next((value['comparison'] for value in results.values() if 'comparison' in value), None)
                reference = True
            timing = f"{report['median_work_seconds']['verilator' if reference else 'cpphdl']:.6f}" if report else '-'
            cycles = str(report['total_cycles']) if report else '-'
            status = 'FAIL' if 'error' in value or report is None else 'PASS'
            print(f'{mode:37} {build:>10} {timing:>18} {cycles:>9}    {status}')
        print('\nOriginal Verilator retains trace formatting; native commit logging is validation-only.')
        print('* Verified cached binary; build time is its original build duration.')
        print(f'Reports and logs: {self.root}')
        if failed:
            raise RuntimeError('one or more builds/comparisons failed; no timing is reported for failed variants')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'convert', 'build', 'run', 'build-all', 'compare'))
    parser.add_argument('mode', nargs='?', choices=MODES)
    parser.add_argument('--work-root', type=Path, default=Path(os.environ.get('CVA6_WORK_ROOT', f'/tmp/cpphdl-cva6-{os.getuid()}')))
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--keep-build-files', action='store_true')
    parser.add_argument('--keep-traces', action='store_true')
    parser.add_argument('--trials', type=int, default=5)
    parser.add_argument('--max-cycles', type=int, default=2_000_000)
    parser.add_argument('--timeout', type=float, default=14400)
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--cpus4')
    args = parser.parse_args()
    if args.trials < 1 or args.max_cycles <= 10 or args.timeout <= 0:
        parser.error('use positive trials/timeout and more than 10 cycles')
    if (args.action in ('build', 'run')) != (args.mode is not None):
        parser.error('build/run require a mode; other actions do not take one')
    workflow = Workflow(args)
    workflow.root.mkdir(parents=True, exist_ok=True)
    marker = workflow.root / '.cva6-workspace'
    if not marker.exists() and any(workflow.root.iterdir()):
        parser.error('work root is nonempty and is not a CVA6 script workspace')
    marker.touch(exist_ok=True)
    with (workflow.root / '.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.action == 'build-all':
            workflow.prepare()
            workflow.convert()
            for mode in MODES:
                workflow.build(mode)
        elif args.action in ('prepare', 'convert', 'compare'):
            getattr(workflow, args.action)()
        else:
            getattr(workflow, args.action)(args.mode)


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(128 + signum))
    try:
        main()
    except (RuntimeError, OSError, ValueError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
