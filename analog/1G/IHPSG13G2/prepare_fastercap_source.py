#!/usr/bin/env python3
"""Build pinned FasterCap with the per-block matrix offset correction."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile

from prepare_fastercap import download, digest, WX_URL, WX_SHA256, WX_PACKAGE

SOURCES = (
    ('FasterCap', 'martinjankoehler/FasterCap', '1955bf75d0cdfbf8eb7e1cf2bd49681e9aaf9218',
     '351d024928b9ea696712d7b0c31379bab1a2163e54efdcec2be2a47084319945'),
    ('LinAlgebra', 'martinjankoehler/LinAlgebra', 'f0ac995736ac3adb73aef67e9ae164a5029a1cd9',
     '57ef87bf0353a5178b999d7cfd46113ca2310240c07f4b98fd21c45197f4b7db'),
    ('Geometry', 'ediloren/Geometry', 'de03ffebfd5013b96102bd60f71c8fe8b73870e2',
     'b6c595dad7cb6b21f0414554d312569b85bbb39750fa542fe41ee2fa277272da'))
PACKAGES = (
    (WX_PACKAGE, WX_URL, WX_SHA256),
    ('headers.deb', 'https://archive.ubuntu.com/ubuntu/pool/universe/w/wxwidgets3.0/wx3.0-headers_3.0.5.1+dfsg-4_all.deb',
     '1d5e1d0f92ed0331091686d7ce95ca3230bcacd12501a64f63218f4a0df5f28a'),
    ('base-dev.deb', 'https://archive.ubuntu.com/ubuntu/pool/universe/w/wxwidgets3.0/libwxbase3.0-dev_3.0.5.1+dfsg-4_amd64.deb',
     '7ce2dc8b8be1c3ebdc60f13f6c3acf2cee06d4df091fcd52740db69ff10a958b'),
    ('blas-dev.deb', 'https://archive.ubuntu.com/ubuntu/pool/main/l/lapack/libblas-dev_3.10.0-2ubuntu1_amd64.deb',
     '8244ba58aad181963a3858a641568cd60c9973de16797d01c7e07e1ce2d868c6'),
    ('blas.deb', 'https://archive.ubuntu.com/ubuntu/pool/main/l/lapack/libblas3_3.10.0-2ubuntu1_amd64.deb',
     '70783f07775e4f5297a364f5c1187c6f8103a8c42dff2c593448c6945610da19'))
BEFORE = '05310bbc04a5a142fb1c72fbcd74b924261a6e304734bb8f5dac12dcefd9ff9e'
AFTER = '44db85f0a495350277655c00e390afb3229745b4873a484d847d73d15724ea19'


def correct_offset(path):
    if digest(path) == AFTER:
        return
    if digest(path) != BEFORE:
        raise ValueError('unrecognized FasterCap multiplication source')
    data = path.read_bytes()
    line = b'    const unsigned long chunkAdjustmentShift = linkChunkNum * m_ulCurrBlock;\r\n'
    anchor = b'        // scan the nodes whose links are in the current block (of course they must be ordered)\r\n'
    if data.count(line) != 1 or data.count(anchor) != 1:
        raise ValueError('changed block-offset implementation')
    data = data.replace(line, b'').replace(anchor,
        b'        // Recompute the offset after loading each interaction block.\r\n' +
        b'        const unsigned long chunkAdjustmentShift = linkChunkNum * m_ulCurrBlock;\r\n\r\n' + anchor)
    path.write_bytes(data)
    if digest(path) != AFTER:
        raise ValueError('unexpected corrected source hash')


def prepare(destination):
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'amd64'):
        raise ValueError('the local development packages require Linux x86-64')
    destination = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    downloads = {}
    for name, repository, commit, checksum in SOURCES:
        archive = destination / (name + '.tgz')
        url = f'https://codeload.github.com/{repository}/tar.gz/{commit}'
        download(url, archive, checksum)
        downloads[url] = checksum
        if not (destination / name).exists():
            with tarfile.open(archive) as source:
                root = source.getnames()[0].split('/')[0]
                source.extractall(destination, filter='data')
            (destination / root).rename(destination / name)
        # A resumed build must still use the pinned sources. The sole allowed
        # local difference is the reviewed correction below.
        with tarfile.open(archive) as source:
            for entry in source.getmembers():
                if not entry.isfile():
                    continue
                relative = Path(entry.name).relative_to(Path(entry.name).parts[0])
                path = destination / name / relative
                expected = hashlib.sha256(source.extractfile(entry).read()).hexdigest()
                allowed = {expected}
                if name == 'FasterCap' and relative.as_posix() == 'Solver/MultiplyHierarchical.cpp':
                    allowed.add(AFTER)
                if not path.is_file() or digest(path) not in allowed:
                    raise ValueError('modified pinned build input: ' + str(path))
    runtime = destination / 'root'
    for name, url, checksum in PACKAGES:
        package = destination / name
        download(url, package, checksum)
        downloads[url] = checksum
        subprocess.run(['dpkg-deb', '-x', str(package), str(runtime)], check=True, timeout=30)
    source = destination / 'FasterCap/Solver/MultiplyHierarchical.cpp'
    correct_offset(source)
    headers = runtime / 'usr/include/x86_64-linux-gnu'
    if not (headers / 'cblas.h').exists():
        (headers / 'cblas.h').symlink_to('cblas-netlib.h')
    wx_config = destination / 'wx-config'
    # CMake calls this wrapper with several different wx-config queries.
    wx_config.write_text('#!/bin/sh\n' +
        'tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n' +
        'exec "$tool_dir/root/usr/lib/x86_64-linux-gnu/wx/config/base-unicode-3.0" ' +
        '--prefix="$tool_dir/root/usr" --exec-prefix="$tool_dir/root/usr" "$@"\n')
    wx_config.chmod(0o755)
    libraries = runtime / 'usr/lib/x86_64-linux-gnu'
    build = destination / 'build'
    commands = [
        ['cmake', '-S', str(destination / 'FasterCap'), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release',
         '-DFASTFIELDSOLVERS_HEADLESS=ON', '-DwxWidgets_CONFIG_EXECUTABLE=' + str(wx_config),
         '-DBLAS_INCLUDE_DIRS=' + str(headers), '-DBLAS_LIBRARIES=' + str(libraries / 'blas/libblas.so.3'),
         '-DCMAKE_EXE_LINKER_FLAGS=-L' + str(libraries / 'blas')],
        ['cmake', '--build', str(build), '-j', '1']]
    for label, command in zip(('configure', 'build'), commands):
        with (destination / (label + '.log')).open('w') as log:
            subprocess.run(['prlimit', '--as=1073741824', '--', *command], stdout=log, stderr=-2,
                           check=True, timeout=900)
    install = destination / 'install'
    install.mkdir(exist_ok=True)
    shutil.copy2(build / 'FasterCap', install / 'FasterCap')
    binary = destination / 'bin/FasterCap'
    binary.parent.mkdir(exist_ok=True)
    binary.write_text('''#!/bin/sh
tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export LD_LIBRARY_PATH="$tool_dir/root/usr/lib/x86_64-linux-gnu:$tool_dir/root/usr/lib/x86_64-linux-gnu/blas${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$tool_dir/install/FasterCap" "$@"
''')
    binary.chmod(0o755)
    files = [install / 'FasterCap', binary, *sorted(runtime.rglob('*.so*'))]
    recipe = Path(__file__).resolve()
    record = dict(status='prepared', version='6.0.9', executable=str(binary), downloads=downloads,
        local_corrections=['per-block-matrix-offset'], correction_source_sha256=dict(before=BEFORE, after=AFTER),
        source_sha256={str(p): digest(p) for p in (recipe, recipe.with_name('prepare_fastercap.py'), source)},
        artifact_sha256={str(p): digest(p) for p in files if p.is_file()},
        scope='source build with block-offset correction; numerical qualification is a separate test')
    (destination / 'prepare.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Prepared corrected source build: ' + str(binary))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    prepare(parser.parse_args().destination)
