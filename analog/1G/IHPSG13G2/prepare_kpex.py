#!/usr/bin/env python3
"""Prepare the pinned GDS-to-field-geometry tool in an isolated Python environment."""
import argparse
import base64
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

SOURCE = Path(__file__).resolve()
LOCK = SOURCE.with_name('kpex-requirements.txt')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(destination):
    hashes = {}
    versions = {}
    for line in LOCK.read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        name = line.split(' @ ')[0]
        package = importlib.metadata.distribution(name)
        versions[name] = package.version
        for entry in package.files:
            if entry.hash is None:
                continue
            path = Path(package.locate_file(entry)).resolve()
            content = path.read_bytes()
            actual = base64.urlsafe_b64encode(hashlib.new(entry.hash.mode, content).digest()).decode().rstrip('=')
            if actual != entry.hash.value:
                raise ValueError('installed wheel content changed: ' + str(path))
            hashes[str(path)] = hashlib.sha256(content).hexdigest()
    import klayout_pex_protobuf
    technology = Path(klayout_pex_protobuf.__file__).parent / 'ihp-sg13g2_tech.pb.json'
    executable = destination / 'venv/bin/kpex'
    hashes[str(executable)] = digest(executable)
    record = dict(status='prepared', version=versions['klayout-pex'], packages=versions,
        executable=str(executable), python=str(destination / 'venv/bin/python'),
        technology=str(technology), artifact_sha256=hashes,
        source_sha256={str(p): digest(p) for p in (SOURCE, LOCK)},
        scope='pinned geometry tool and its bundled technology; independent PDK and field calibration required')
    (destination / 'prepare.json').write_text(json.dumps(record, indent=2) + '\n')


def prepare(destination):
    if (sys.version_info[:2] != (3, 14) or platform.system() != 'Linux'
            or platform.machine() not in ('amd64', 'x86_64')):
        raise ValueError('the locked wheels require Linux x86-64 CPython 3.14')
    destination = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    python = destination / 'venv/bin/python'
    if not python.exists():
        subprocess.run([sys.executable, '-m', 'venv', str(destination / 'venv')], check=True)
    environment = dict(os.environ, PIP_DISABLE_PIP_VERSION_CHECK='1')
    certificates = Path('/etc/ssl/certs/ca-certificates.crt')
    if certificates.exists():
        environment['PIP_CERT'] = str(certificates)
    with (destination / 'install.log').open('w') as log:
        subprocess.run([str(python), '-m', 'pip', 'install', '--no-cache-dir', '--require-hashes',
            '--only-binary=:all:', '-r', str(LOCK)], env=environment, stdout=log, stderr=-2,
            check=True, timeout=900)
    subprocess.run([str(python), str(SOURCE), '--inventory', '--destination', str(destination)], check=True)
    print('Prepared pinned KLayout-PEX: ' + str(destination / 'prepare.json'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--inventory', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    (inventory if args.inventory else prepare)(args.destination.resolve())
