#!/usr/bin/env python3
"""Install isolated KLayout 0.30.12 for native SRAM LVS without changing the PDK venv."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import ssl
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
VERSION = '0.30.12'
URL = 'https://www.klayout.org/downloads/Ubuntu-22/klayout_0.30.12-1_amd64.deb'
SHA256 = 'c09637de6d82bfa2a102c2468e285d3d060fa59b86680f10393a0e9f69ab5763'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare(args):
    destination = args.output.resolve()
    archive = destination / Path(URL).name
    base = ROOT / 'out/repeater-toolchain/root'
    if not (base / 'usr/lib/x86_64-linux-gnu').is_dir():
        raise ValueError('prepare the repeater toolchain before installing the isolated LVS tool')
    if not args.verify_only:
        destination.mkdir(parents=True, exist_ok=True)
        if not archive.is_file():
            temporary = archive.with_suffix('.download')
            certificates = Path('/etc/ssl/certs/ca-certificates.crt')
            context = ssl.create_default_context(cafile=str(certificates) if certificates.is_file() else None)
            with urllib.request.urlopen(URL, context=context) as response, temporary.open('wb') as stream:
                shutil.copyfileobj(response, stream)
            if digest(temporary) != SHA256:
                temporary.unlink()
                raise ValueError('KLayout archive checksum mismatch')
            temporary.rename(archive)
    if not archive.is_file() or digest(archive) != SHA256:
        raise ValueError('missing or changed KLayout archive')
    wrapper = destination / 'bin/klayout'
    if not args.verify_only:
        subprocess.run(['dpkg-deb', '-x', str(archive), str(destination / 'root')], check=True)
        wrapper.parent.mkdir(exist_ok=True)
        environment = {
            'LD_LIBRARY_PATH': str(destination / 'root/usr/lib/klayout') + ':' + str(base / 'usr/lib/x86_64-linux-gnu'),
            'RUBYLIB': str(base / 'usr/lib/ruby/3.0.0') + ':' + str(base / 'usr/lib/x86_64-linux-gnu/ruby/3.0.0'),
            'QT_QPA_PLATFORM': 'offscreen',
        }
        wrapper.write_text('#!/bin/sh\n' + ''.join('export ' + key + '=' + shlex.quote(value) + '\n'
                                               for key, value in environment.items()) +
                           'exec ' + shlex.quote(str(destination / 'root/usr/bin/klayout')) + ' "$@"\n')
        wrapper.chmod(0o755)
        # pip selects the wheel matching the prepared interpreter's ABI. The
        # installation stays in this tool directory, outside the PDK venv.
        subprocess.run([str(args.python), '-m', 'pip', 'install', '--disable-pip-version-check',
                        '--no-deps', '--only-binary=:all:', '--target', str(destination / 'python'),
                        'klayout==' + VERSION], check=True)
    binary_version = subprocess.check_output([str(wrapper), '-v'], text=True).strip()
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONPATH=str(destination / 'python'))
    script = 'import klayout, klayout.dbcore; print(klayout.__version__); print(klayout.dbcore.__file__)'
    lines = subprocess.check_output([str(args.python), '-c', script], env=environment, text=True).splitlines()
    if binary_version != 'KLayout ' + VERSION or len(lines) != 2 or lines[0] != VERSION:
        raise ValueError('isolated KLayout version mismatch')
    module = Path(lines[1]).resolve()
    if not module.is_relative_to(destination / 'python'):
        raise ValueError('Python imported a different KLayout installation')
    result = dict(version=VERSION, archive_url=URL, archive_sha256=SHA256,
                  python=str(args.python.resolve()), python_module=str(module), python_module_sha256=digest(module),
                  executable_sha256=digest(destination / 'root/usr/bin/klayout'))
    if not args.verify_only:
        (destination / 'setup_receipt.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/klayout-lvs-0.30.12')
    parser.add_argument('--python', type=Path, default=ROOT / 'pdk/IHPSG13G2/.cpphdl/venv/bin/python')
    parser.add_argument('--verify-only', action='store_true')
    prepare(parser.parse_args())
