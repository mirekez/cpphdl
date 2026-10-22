#!/usr/bin/env python3
"""Read-only validation of the integration's committed revision lock."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


def git(repo, *args):
    result = subprocess.run(['git', '-C', str(repo), *args], capture_output=True)
    if result.returncode:
        raise ValueError(f'{repo}: {result.stderr.decode().strip()}')
    return result.stdout


def load_lock():
    lock = json.loads(Path(__file__).with_name('revisions.json').read_text())
    revisions = [lock['chipyard']['commit'], lock['circt']['commit'],
                 *lock['chipyard']['overrides'].values()]
    if any(not isinstance(revision, str) or not re.fullmatch('[0-9a-f]{40}', revision)
           for revision in revisions):
        raise ValueError('revisions.json must contain full commit SHAs, not branches or tags')
    for path in [*lock['chipyard']['required_submodules'], *lock['chipyard']['overrides']]:
        if PurePosixPath(path).is_absolute() or '..' in PurePosixPath(path).parts:
            raise ValueError(f'invalid locked submodule path: {path}')
    return lock


def check_repo(root, spec):
    required = set(spec.get('required_submodules', []))
    overrides = spec.get('overrides', {})
    checked = set()
    declared = set()

    def visit(repo, expected, path='', ancestors=()):
        actual = git(repo, 'rev-parse', 'HEAD').decode().strip()
        if actual != expected:
            raise ValueError(f'{repo}: revision {actual}, required {expected}; checkout left untouched')
        real = repo.resolve()
        if real in ancestors:
            raise ValueError(f'{repo}: cyclic submodule checkout')
        checked.add(path)
        # Read the pinned tree, not the mutable index or .gitmodules file.
        prefix = git(repo, 'rev-parse', '--show-prefix').decode().strip()
        tree = git(repo, 'ls-tree', '-rz', '--full-tree', expected)
        for entry in tree.split(b'\0'):
            if not entry:
                continue
            info, name = entry.split(b'\t', 1)
            mode, kind, revision = info.decode().split()
            name = name.decode()
            if mode != '160000' or kind != 'commit' or not name.startswith(prefix):
                continue
            name = name[len(prefix):]
            relative = f'{path}/{name}' if path else name
            declared.add(relative)
            child = repo / name
            if not (child / '.git').exists():
                if relative in required or any(relative.startswith(parent + '/') for parent in required):
                    raise ValueError(f'{child}: required submodule is not initialized')
                if child.exists() and (not child.is_dir() or any(
                        item.is_symlink() or not item.is_dir() for item in child.rglob('*'))):
                    raise ValueError(f'{child}: populated submodule has no Git checkout')
                continue
            visit(child, overrides.get(relative, revision), relative, (*ancestors, real))

    visit(root, spec['commit'])
    if required - checked:
        raise ValueError('required submodules missing: ' + ', '.join(sorted(required - checked)))
    if set(overrides) - declared:
        raise ValueError('undeclared submodule overrides: ' + ', '.join(sorted(set(overrides) - declared)))
    return len(checked)


def check_sdk(root, version):
    for package in ('circt', 'mlir', 'llvm'):
        filename = {'circt': 'CIRCT', 'mlir': 'MLIR', 'llvm': 'LLVM'}[package] + 'Config.cmake'
        if not (root / 'lib/cmake' / package / filename).is_file():
            raise ValueError(f'{root}: missing {package} SDK package')
    result = subprocess.run([str(root / 'bin/firtool'), '--version'], text=True, capture_output=True)
    if result.returncode or not re.search(r'^CIRCT ' + re.escape(version) + r'\s*$',
                                         result.stdout + result.stderr, re.MULTILINE):
        raise ValueError(f'{root}: required CIRCT SDK {version}, got {result.stdout.strip()}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', type=Path)
    parser.add_argument('--component', choices=('chipyard', 'circt', 'sdk'), default='chipyard')
    parser.add_argument('--print-version', choices=('chipyard', 'circt', 'sdk'))
    parser.add_argument('--quiet', action='store_true')
    args = parser.parse_args()
    try:
        lock = load_lock()
        if args.print_version:
            print(lock['circt']['sdk_version'] if args.print_version == 'sdk'
                  else lock[args.print_version]['commit'])
            return 0
        if args.root is None:
            parser.error('root is required for validation')
        if args.component == 'sdk':
            check_sdk(args.root, lock['circt']['sdk_version'])
            description = f'CIRCT SDK {lock["circt"]["sdk_version"]}'
        else:
            count = check_repo(args.root, lock[args.component])
            description = f'{args.component} ({count} {"repository" if count == 1 else "repositories"})'
        if not args.quiet:
            print(f'Pinned {description} verified: {args.root}')
        return 0
    except (ValueError, OSError, KeyError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
