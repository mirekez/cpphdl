#!/usr/bin/env python3
"""Build pinned Magic locally for SG13G2 parasitic extraction."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

COMMIT = 'bed8d4ff84333767aa923d469d339cd05c56a00a'
ARCHIVE_SHA256 = 'a11f682818339061c198467c44d33dea8ccc38b7fa4aa7be1d5dadbd77e27719'
TK_PACKAGES = {
    'tk8.6-dev': '75339be5a4689b153fb9ebfa0e4abf948134e7922edc42a6d3addce5ec3086fc',
    'libtk8.6': 'fd394fe34bce405d66079518f122917e434dc269315ed4125dc982d31a03684d',
}


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            result.update(block)
    return result.hexdigest()


def preserve_small_values(source, precise_values=False):
    """Keep sub-attofarad values which the pinned SI formatter rounds to zero.

    This changes decimal serialization only. The original esSIvalue leaves
    values below 1e-18 unscaled, then prints them with fixed decimal places.
    Distributed networks can lose hundreds of these real extracted loads.
    """
    path = source / 'ext2spice/ext2spice.c'
    before = digest(path)
    text = path.read_text()
    original = '    if (avalue < 1.0E-18)\n    {\n\t/* Do nothing---value is probably zero */\n    }'
    replacement = ('    if (avalue < 1.0E-18)\n    {\n'
                   '\t/* Preserve the extracted float, including sub-attofarad loads. */\n'
                   '\tfprintf(file, "%.8g", (double)value);\n\treturn;\n    }')
    if text.count(original) != 1:
        raise ValueError('pinned Magic SI formatter differs from the reviewed correction')
    text = text.replace(original, replacement)
    if precise_values:
        formatter = '    nDecimals(vstr, (double)value, 5);\n    morphNumericString(vstr, 5);'
        if text.count(formatter) != 1:
            raise ValueError('pinned Magic decimal formatter differs from the reviewed correction')
        # Fixed decimal places accumulate independent rounding on the ground
        # and coupling records. Nine significant digits retain float precision
        # at every SI scale without changing the capacitance acceptance bounds.
        text = text.replace(formatter, '    snprintf(vstr, sizeof(vstr), "%.9g", (double)value);')
        text = text.replace('fprintf(file, "%.8g", (double)value);',
                            'fprintf(file, "%.9g", (double)value);')
    path.write_text(text)
    return dict(file='ext2spice/ext2spice.c', before_sha256=before, after_sha256=digest(path),
                reason=('nine significant digits across SI scales' if precise_values else
                        'scientific formatting below 1e-18') + '; no extraction coefficient or geometry changes')


def preserve_double_values(source):
    """Do not narrow double-valued geometry before decimal serialization."""
    records = []
    for relative, replacements in (
        ('ext2spice/ext2spice.c', (
            ('float esScale = -1.0 ;', 'double esScale = -1.0 ;'),
            ('esSIvalue(\n    FILE *file,\n    float value)',
             'esSIvalue(\n    FILE *file,\n    double value)'),
            ('    float avalue;\n\n    avalue = fabsf(value);',
             '    double avalue;\n\n    avalue = fabs(value);'))),
        ('ext2spice/ext2spice.h', (
            ('extern float esScale;', 'extern double esScale;'),
            ('extern void esSIvalue(FILE *file, float value);',
             'extern void esSIvalue(FILE *file, double value);'),))):
        path = source / relative
        before = digest(path)
        text = path.read_text()
        for old, new in replacements:
            if text.count(old) != 1:
                raise ValueError('pinned Magic SI argument differs: ' + relative)
            text = text.replace(old, new)
        path.write_text(text)
        records.append(dict(path=relative, before_sha256=before, after_sha256=digest(path)))
    return dict(files=records, reason='retain double geometry scale, arguments and SI scaling in serializer; '
                'no geometry snapping or extraction coefficient changes')


def prepare(destination, clip_tile_area=False, full_sidewall=False, precise_values=False,
            double_values=False):
    if double_values and not precise_values:
        raise ValueError('double serialization requires precise decimal output')
    root = destination.resolve()
    root.mkdir(parents=True, exist_ok=True)
    downloads = {}

    def download(url, name, expected):
        path = root / name
        if not path.exists():
            temporary = path.with_suffix(path.suffix + '.download')
            subprocess.run(['curl', '--fail', '--location', '--silent', '--show-error',
                            '--retry', '3', url, '-o', str(temporary)], check=True, timeout=300)
            if digest(temporary) != expected:
                raise ValueError('download digest mismatch: ' + name)
            temporary.replace(path)
        if digest(path) != expected:
            raise ValueError('cached archive digest mismatch: ' + name)
        downloads[str(path)] = dict(url=url, sha256=expected)
        return path

    archive = download('https://codeload.github.com/RTimothyEdwards/magic/tar.gz/' + COMMIT,
                       'source.tar.gz', ARCHIVE_SHA256)
    # A fresh verified source tree prevents local edits in a cached build from
    # being silently reported as the upstream commit.
    build = Path(tempfile.mkdtemp(prefix='build-', dir=root))
    subprocess.run(['tar', '-xzf', str(archive), '-C', str(build)], check=True)
    source = build / ('magic-' + COMMIT)
    adjustments = [preserve_small_values(source, precise_values)]
    if double_values:
        adjustments.append(preserve_double_values(source))
    if clip_tile_area:
        from magic_tile_area import apply
        adjustments.append(apply(source))
    if full_sidewall:
        from magic_sidewall import apply
        adjustments.append(apply(source))
    prefix = root / 'install'
    options = ['--prefix=' + str(prefix), '--with-interpreter=tcl', '--without-x',
               '--without-opengl', '--without-cairo', '--disable-readline', '--disable-magic-builddate']
    # Even the headless build compiles Magic's Tk launcher. Keep missing Ubuntu
    # build dependencies under out/; never install packages into the host OS.
    if not Path('/usr/include/tcl8.6/tk.h').exists():
        release = Path('/etc/os-release').read_text()
        if platform.machine() != 'x86_64' or 'ID=ubuntu\n' not in release or 'VERSION_ID="22.04"' not in release:
            raise ValueError('install Tcl/Tk development packages for this host before building Magic')
        deps = root / 'dependencies'
        for package, expected in TK_PACKAGES.items():
            name = package + '_8.6.12-1build1_amd64.deb'
            path = download('https://archive.ubuntu.com/ubuntu/pool/main/t/tk8.6/' + name, name, expected)
            subprocess.run(['dpkg-deb', '-x', str(path), str(deps)], check=True)
        options += ['--with-tk=' + str(deps / 'usr/lib/x86_64-linux-gnu/tk8.6'),
                    '--with-tkincls=' + str(deps / 'usr/include/tcl8.6'),
                    '--with-tklibs=' + str(deps / 'usr/lib/x86_64-linux-gnu')]
    env = dict(os.environ, OMP_NUM_THREADS='1')
    for name, command in [('configure', ['./configure', *options]),
                          ('build', ['make', '-j1']), ('install', ['make', 'install'])]:
        with (root / (name + '.log')).open('w') as log:
            subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=1800)
    magic = prefix / 'bin/magic'
    version = subprocess.check_output([str(magic), '-dnull', '-noconsole', '--version'], text=True).strip()
    if version != '8.3.685':
        raise ValueError('unexpected extraction tool version: ' + version)
    artifacts = {str(p): digest(p) for p in prefix.rglob('*') if p.is_file()}
    report = dict(status='pass', version=version, commit=COMMIT, downloads=downloads, source=str(source),
                  source_adjustments=adjustments, recipe_sha256=digest(__file__),
                  configure=options, artifact_sha256=artifacts)
    (root / 'prepare.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Prepared Magic ' + version + ': ' + str(magic))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'out/magic-toolchain')
    parser.add_argument('--clip-tile-area', action='store_true',
                        help='bound distributed capacitance area weights to conductor tile geometry')
    parser.add_argument('--full-sidewall', action='store_true',
                        help='remove the duplicate sidewall parser halving; requires separate coefficient qualification')
    parser.add_argument('--precise-values', action='store_true',
                        help='preserve nine significant digits instead of five fixed decimal places in SPICE values')
    parser.add_argument('--double-values', action='store_true',
                        help='retain double precision through SPICE serialization; requires --precise-values')
    args = parser.parse_args()
    prepare(args.destination, args.clip_tile_area, args.full_sidewall, args.precise_values, args.double_values)
