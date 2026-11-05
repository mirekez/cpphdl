"""Optimizer shell tests must use the configured compiler, including wrappers."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ctest', required=True)
    parser.add_argument('--build', required=True)
    parser.add_argument('--cxx', required=True)
    args = parser.parse_args()
    result = subprocess.run([args.ctest, '--test-dir', args.build, '--show-only=json-v1'],
                            check=True, capture_output=True, text=True)
    tests = json.loads(result.stdout)['tests']
    selected = None
    checked = 0
    for test in tests:
        command = test.get('command', [])
        for argument in command:
            script = Path(argument)
            if script.parent.name == 'optimizer' and script.name.endswith('Test.sh'):
                assert args.cxx in command, (test['name'], 'missing configured compiler')
                assert not re.search(r'\bg\+\+\s+-std=', script.read_text()), script
                checked += 1
        if test['name'] == 'optimizer_constexpr_binding':
            selected = command
    assert checked >= 15 and selected, 'Missing optimizer test registrations'

    with tempfile.TemporaryDirectory(prefix='optimizer-compiler-') as directory:
        work = Path(directory)
        calls = work / 'calls'
        compiler = work / 'compiler wrapper'
        compiler.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> ' + shlex.quote(str(calls))
                            + '\nexec ' + shlex.quote(args.cxx) + ' "$@"\n')
        compiler.chmod(0o755)
        forbidden = work / 'g++'
        forbidden.write_text('#!/bin/sh\necho "Unexpected PATH g++" >&2\nexit 97\n')
        forbidden.chmod(0o755)
        env = dict(os.environ, PATH=str(work) + os.pathsep + os.environ['PATH'])
        command = [str(compiler) if arg == args.cxx else arg for arg in selected]
        subprocess.run(command, env=env, check=True, timeout=120)
        invocations = calls.read_text().splitlines()
        assert any(' -c ' in ' ' + call + ' ' for call in invocations), invocations
        assert any(' -c ' not in ' ' + call + ' ' for call in invocations), invocations
    print(f'{checked} optimizer scripts use the configured compiler; compile/link wrapper exercised')


if __name__ == '__main__':
    main()
