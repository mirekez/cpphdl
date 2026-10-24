"""Bound builds, regressions and EDA processes to one memory budget."""
import os
import fcntl
from contextlib import contextmanager
from pathlib import Path
import signal
import subprocess
import sys

MEMORY_LOCK = Path(__file__).resolve().parents[2] / 'out/eda-memory.lock'
SLOT_OWNER = 'CPPHDL_MEMORY_SLOT_OWNER'


def inherited_slot():
    """A regression owning the slot may launch nested EDA runners without deadlock."""
    owner = os.environ.get(SLOT_OWNER, '')
    if not owner.isdecimal():
        return False
    try:
        if MEMORY_LOCK.read_text().strip() != owner:
            return False
        ancestor = os.getppid()
        while ancestor > 1:
            if ancestor == int(owner):
                return True
            status = Path('/proc') / str(ancestor) / 'status'
            ancestor = int(next(line.split()[1] for line in status.read_text().splitlines()
                                if line.startswith('PPid:')))
    except (OSError, ValueError, StopIteration):
        pass
    return False


@contextmanager
def memory_slot(command, exclusive=None):
    names = {Path(str(part)).name for part in command[:2]}
    heavy = bool(names & {'openroad', 'yosys', 'verilator', 'klayout', 'openvaf', 'openvaf-r', 'run_drc.py', 'run_lvs.py',
                         'cmake', 'ctest', 'make', 'gmake', 'ninja', 'c++', 'g++', 'clang++',
                         'cc1plus', 'cpphdl'})
    # Dependency-only preprocessing builds no AST, object or EDA database. It
    # remains bounded while a large simulation or physical run owns the slot.
    if names & {'c++', 'g++', 'clang++'} and any(str(arg) in ('-M', '-MM') for arg in command[1:]):
        heavy = False
    if inherited_slot():
        yield False
        return
    if exclusive is False or (exclusive is None and not heavy):
        yield False
        return
    MEMORY_LOCK.parent.mkdir(parents=True, exist_ok=True)
    with MEMORY_LOCK.open('a+') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('Waiting for shared EDA memory slot: ' + Path(str(command[0])).name, file=sys.stderr, flush=True)
            fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            lock.seek(0)
            lock.truncate()
            lock.write(str(os.getpid()) + '\n')
            lock.flush()
            yield True
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def execute(command, *, check=False, timeout=None, memory_exclusive=None, **options):
    with memory_slot(command, memory_exclusive) as owned:
        if owned:
            environment = dict(options.pop('env', os.environ))
            environment[SLOT_OWNER] = str(os.getpid())
            options['env'] = environment
        process = subprocess.Popen(command, start_new_session=True, **options)
        try:
            result = process.wait(timeout=timeout)
        except BaseException:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise
    if check and result:
        raise subprocess.CalledProcessError(result, command)
    return subprocess.CompletedProcess(command, result)


def configure_cleanup():
    # CTest can terminate the runner while its EDA child has a separate group.
    # Converting SIGTERM to an exception lets execute() stop that group too.
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)


if __name__ == '__main__':
    import argparse
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exclusive', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('an EDA command is required')
    raise SystemExit(execute(command, memory_exclusive=True if args.exclusive else None).returncode)
