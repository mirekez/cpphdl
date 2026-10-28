#!/usr/bin/env python3
"""Exercise upstream SI serialization with exact-grid MOS geometry arguments."""
import argparse
import hashlib
import math
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_magic import ARCHIVE_SHA256, COMMIT, preserve_small_values, preserve_double_values


def run(args):
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise ValueError('unverified upstream formatter archive')
    with tempfile.TemporaryDirectory(prefix='magic-double-formatter-') as directory:
        root = Path(directory)
        (root / 'ext2spice').mkdir()
        with tarfile.open(args.archive) as archive:
            for filename in ('ext2spice.c', 'ext2spice.h'):
                (root / 'ext2spice' / filename).write_bytes(
                    archive.extractfile('magic-' + COMMIT + '/ext2spice/' + filename).read())
        preserve_small_values(root, precise_values=True)
        preserve_double_values(root)
        source = (root / 'ext2spice/ext2spice.c').read_text()
        function = re.findall(r'void\nesSIvalue\(.*?^}', source, re.M | re.S)
        if len(function) != 1:
            raise ValueError('ambiguous upstream formatter function')
        # These are the real double expressions used at device emission sites.
        # 0.45 um previously became 0.449999988 um through a float argument.
        values = [.45e-6, .13e-6, 10e-6, .005e-6, 3.444e-18, 1.74487e-18,
                  1.88144e-18, .389794e-18, .329716e-18, 7.78982e-18]
        values += [sign * factor * 10. ** exponent for sign in (-1, 1)
                   for factor in (.9998, 1.0002, 3.14159265) for exponent in range(-30, 13, 3)]
        scale = re.findall(r'^double esScale = -1.0 ;', source, re.M)
        if len(scale) != 1:
            raise ValueError('geometry output scale still narrows the physical grid')
        code = ('#include <stdio.h>\n#include <math.h>\n' + scale[0] + '\n' + function[0] + '\nint main(void) {\n'
            'double values[] = {' + ','.join(v.hex() for v in values) + '};\n'
            'for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {\n'
            'esSIvalue(stdout, values[i]); putchar(10); }\n'
            'float EFScale = 0.5f; esScale = EFScale / 100.0;\n'
            'esSIvalue(stdout, 1.0E-6 * 90 * esScale); putchar(10);\n'
            'esSIvalue(stdout, 1.0E-6 * 26 * esScale); putchar(10); return 0; }\n')
        (root / 'formatter.c').write_text(code)
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Werror', root / 'formatter.c', '-lm',
                        '-o', root / 'formatter'], check=True, timeout=60)
        output = subprocess.check_output([root / 'formatter'], text=True, timeout=10).splitlines()
        scales = dict(f=1e-15, p=1e-12, n=1e-9, u=1e-6, m=1e-3, k=1e3, G=1e9)
        parsed = [float(v[:-1]) * scales[v[-1]] if v[-1] in scales else float(v) for v in output]
        values += [.45e-6, .13e-6]
        if len(parsed) != len(values) or any(not math.isclose(a, b, rel_tol=1e-9, abs_tol=0)
                                            for a, b in zip(parsed, values)):
            raise ValueError('double serialization changed arguments beyond decimal precision')
        if any(not math.isclose(a, b, rel_tol=1e-14) for a, b in zip(parsed[:4], values[:4])):
            raise ValueError('grid-exact geometry lost precision in formatter')
        if any(not math.isclose(a, b, rel_tol=1e-14) for a, b in zip(parsed[-2:], values[-2:])):
            raise ValueError('physical-grid scaling changed minimum MOS dimensions')
        if abs(sum(parsed[4:9]) - parsed[9]) > 1e-24:
            raise ValueError('small-capacitance charge sum changed')
        try:
            preserve_double_values(root)
        except ValueError:
            pass
        else:
            raise ValueError('unexpected upstream argument declaration accepted')
    print('PASS actual double SI formatter:', len(values), 'values, exact geometry and charge sum')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    run(parser.parse_args())
