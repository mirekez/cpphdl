"""Check exclusion and inherited ownership without running an EDA tool."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(__file__).resolve().parents[1] / 'tool_process.py'
spec = importlib.util.spec_from_file_location('repeater_tool_process', source)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

with tempfile.TemporaryDirectory() as directory:
    runner.MEMORY_LOCK = Path(directory) / 'memory.lock'
    environment = dict(os.environ)
    code = '''
import importlib.util, pathlib, sys
spec = importlib.util.spec_from_file_location('runner', sys.argv[1])
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
runner.MEMORY_LOCK = pathlib.Path(sys.argv[2])
assert runner.inherited_slot()
runner.execute([sys.executable, '-c', 'print("nested slot passed")'], memory_exclusive=True, timeout=3, check=True)
'''
    with runner.memory_slot(['cmake']) as owned:
        assert owned
        # Discovery must not deadlock behind a regression's memory reservation.
        with runner.memory_slot(['c++', '-MM', 'source.cpp']) as preprocessing:
            assert not preprocessing
        # A separate family must block while the build owns the slot.
        probe = subprocess.run([sys.executable, '-c',
            'import fcntl,sys; f=open(sys.argv[1], "a+"); fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)',
            str(runner.MEMORY_LOCK)], capture_output=True)
        assert probe.returncode != 0 and b'BlockingIOError' in probe.stderr
        environment[runner.SLOT_OWNER] = str(os.getpid())
        subprocess.run([sys.executable, '-c', code, str(source), str(runner.MEMORY_LOCK)],
                       env=environment, check=True, timeout=5)
    with runner.memory_slot(['ctest']) as owned:
        assert owned
    with runner.memory_slot(['python3', 'ordinary.py']) as owned:
        assert not owned
    environment[runner.SLOT_OWNER] = '1'
    subprocess.run([sys.executable, '-c',
        'import fcntl,sys; f=open(sys.argv[1], "a+"); fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)',
        str(runner.MEMORY_LOCK)], check=True, env=environment)
print('PASS build/regression exclusion, nested ownership and lock release')
