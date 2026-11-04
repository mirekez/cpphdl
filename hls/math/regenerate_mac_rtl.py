#!/usr/bin/env python3
"""Regenerate the operation-level MAC RTL from its C++RTL reference modules."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cpphdl',type=Path,required=True)
    p.add_argument('--check',action='store_true')
    a=p.parse_args();math=Path(__file__).resolve().parent;root=math.parents[1]
    with tempfile.TemporaryDirectory(prefix='cpphdl-mac-rtl-') as directory:
        subprocess.run([str(a.cpphdl.resolve()),'--generated-dir',directory,
            str(root/'hls/tests/Fp32MacRtl.cpp'),'--','-I'+str(root/'include')],check=True)
        for name,source in [('FpAdd.sv','lib/math/FpAdd.h'),('Fp32Mac.sv','lib/math/Fp32Mac.h')]:
            text=(Path(directory)/name).read_text().replace('import Predef_pkg::*;','')
            text='// Generated from '+source+' by cpphdl.\n'+text
            target=math/'rtl'/name
            if a.check:
                if target.read_text()!=text:raise RuntimeError('Stale generated MAC RTL: '+str(target))
            else:target.write_text(text)


if __name__=='__main__':main()
