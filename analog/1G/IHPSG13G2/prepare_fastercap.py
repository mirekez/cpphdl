#!/usr/bin/env python3
"""Prepare a pinned, local headless field solver for extraction calibration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import ssl
import subprocess
import tarfile
import urllib.request

ARCHIVE = 'FasterCap-6.0.9.linux-ubuntu22-amd64.tar.gz'
URL = 'https://github.com/martinjankoehler/FasterCap/releases/download/v6.0.9/' + ARCHIVE
SHA256 = '04cd018c854ce66cad4eaf98af7b54f228bb60b91a75aba2d584f28b9e0e4a19'
WX_PACKAGE = 'libwxbase3.0-0v5_3.0.5.1+dfsg-4_amd64.deb'
WX_URL = 'https://archive.ubuntu.com/ubuntu/pool/universe/w/wxwidgets3.0/' + WX_PACKAGE
WX_SHA256 = 'b2945e5ede5f2ff707776d08601f89c574c9a8564271d6a0692d6ee852aa0717'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(url, path, expected):
    if not path.exists():
        # Conda Python may use a different trust store from the host tools.
        certificates = Path('/etc/ssl/certs/ca-certificates.crt')
        context = ssl.create_default_context(cafile=str(certificates) if certificates.exists() else None)
        with urllib.request.urlopen(url, context=context, timeout=60) as source:
            payload = source.read(4 * 1024 * 1024)
            if source.read(1):
                raise ValueError('unexpectedly large field-solver archive')
        if hashlib.sha256(payload).hexdigest() != expected:
            raise ValueError('download checksum mismatch: ' + url)
        path.write_bytes(payload)
    if digest(path) != expected:
        raise ValueError('cached download checksum mismatch: ' + str(path))


def prepare(destination):
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'amd64'):
        raise ValueError('this pinned binary recipe requires Linux x86-64')
    destination = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    archive, package = destination / ARCHIVE, destination / WX_PACKAGE
    download(URL, archive, SHA256)
    download(WX_URL, package, WX_SHA256)
    install = destination / 'install'
    install.mkdir(exist_ok=True)
    with tarfile.open(archive) as bundle:
        entries = bundle.getmembers()
        if len(entries) != 1 or entries[0].name != 'FasterCap' or not entries[0].isfile():
            raise ValueError('changed field-solver archive contents')
        executable = install / 'FasterCap'
        executable.write_bytes(bundle.extractfile(entries[0]).read())
        executable.chmod(0o755)
    root = destination / 'root'
    subprocess.run(['dpkg-deb', '-x', str(package), str(root)], check=True, timeout=30)
    binary = destination / 'bin/FasterCap'
    binary.parent.mkdir(exist_ok=True)
    binary.write_text('''#!/bin/sh
solver_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export LD_LIBRARY_PATH="$solver_dir/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$solver_dir/install/FasterCap" "$@"
''')
    binary.chmod(0o755)
    environment = dict(os.environ, LD_LIBRARY_PATH=str(root / 'usr/lib/x86_64-linux-gnu'))
    dependencies = subprocess.run(['ldd', str(executable)], env=environment, capture_output=True,
                                  text=True, check=True, timeout=30).stdout
    if 'not found' in dependencies:
        raise ValueError('missing field-solver runtime dependency: ' + dependencies)
    (destination / 'dependencies.txt').write_text(dependencies)
    record = dict(status='prepared', version='6.0.9', executable=str(binary),
        downloads={URL: SHA256, WX_URL: WX_SHA256}, recipe_sha256=digest(Path(__file__).resolve()),
        artifact_sha256={str(p): digest(p) for p in [executable, binary, *sorted(root.rglob('*.so.*'))]
                        if p.is_file()},
        scope='local tool preparation only; numerical and technology calibration require separate tests')
    (destination / 'prepare.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Prepared pinned FasterCap: ' + str(binary))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    prepare(parser.parse_args().destination)
