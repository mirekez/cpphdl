#!/usr/bin/env python3
"""Execute the patched pinned C formatter against small-capacitance regressions."""
import argparse
import hashlib
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_magic import ARCHIVE_SHA256, COMMIT, preserve_small_values


def run(args):
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise ValueError('formatter test requires the verified upstream archive')
    with tarfile.open(args.archive) as archive:
        original = archive.extractfile('magic-' + COMMIT + '/ext2spice/ext2spice.c').read().decode()
    with tempfile.TemporaryDirectory(prefix='magic-formatter-') as directory:
        root = Path(directory)
        (root / 'ext2spice').mkdir()
        path = root / 'ext2spice/ext2spice.c'
        path.write_text(original)
        preserve_small_values(root, precise_values=True)
        function = re.findall(r'void\nesSIvalue\(.*?^}', path.read_text(), re.M | re.S)
        if len(function) != 1:
            raise ValueError('ambiguous actual upstream C formatter')
        values = [0., 3.444e-18, 1.74487e-18, 1.88144e-18, .389794e-18, .329716e-18, 7.78982e-18]
        values += [sign * factor * 10. ** exponent for sign in (-1, 1)
                   for factor in (.9998, 1.0002, 3.14159265) for exponent in range(-30, 13, 3)]
        values = [struct.unpack('f', struct.pack('f', v))[0] for v in values]
        source = ('#include <stdio.h>\n#include <math.h>\n' + function[0] + '\nint main(void) {\n'
            'float values[] = {' + ','.join(v.hex() + 'f' for v in values) + '};\n'
            'for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {\n'
            'esSIvalue(stdout, values[i]); putchar(10); } return 0; }\n')
        (root / 'formatter.c').write_text(source)
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Werror', str(root / 'formatter.c'), '-lm', '-o', str(root / 'formatter')], check=True, timeout=60)
        output = subprocess.check_output([str(root / 'formatter')], text=True, timeout=10).splitlines()
        scales = {'f': 1e-15, 'p': 1e-12, 'n': 1e-9, 'u': 1e-6, 'm': 1e-3, 'k': 1e3, 'G': 1e9}
        parsed = [float(v[:-1]) * scales[v[-1]] if v[-1] in scales else float(v) for v in output]
        if len(parsed) != len(values) or any(not math.isclose(a, b, rel_tol=2e-7, abs_tol=0) for a, b in zip(parsed, values)):
            raise ValueError('SI serialization lost single-precision values')
        if abs(sum(parsed[1:6]) - parsed[6]) > 1e-24:
            raise ValueError('small PFD capacitance records no longer sum within extraction precision')
        # The source patch must reject unreviewed formatter changes.
        path.write_text(original.replace('morphNumericString(vstr, 5);', 'morphNumericString(vstr, 4);'))
        try:
            preserve_small_values(root, precise_values=True)
        except ValueError:
            pass
        else:
            raise ValueError('unreviewed upstream formatter accepted')
    print(f'PASS actual patched C formatter: {len(values)} values, PFD charge sum and changed-source rejection')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    run(parser.parse_args())
