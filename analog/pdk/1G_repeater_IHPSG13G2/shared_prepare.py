#!/usr/bin/env python3
"""Rebuild analog leaves and the shared floorplan input from C++ source."""
import argparse
from pathlib import Path
import sys
from tool_process import configure_cleanup, execute
from route_tech import prepare

HERE=Path(__file__).resolve().parent
ANALOG=HERE.parents[1]


def run(args):
    work=args.work.resolve()
    work.mkdir(parents=True,exist_ok=True)
    prepare(args.pdk.resolve(), work)
    python=args.pdk.resolve()/'.cpphdl/venv/bin/python'
    commands=[('export-build',[args.cxx,'-std=c++20','-O1','-I'+str(ANALOG/'include'),'-I'+str(ANALOG),
                 '-I'+str(ANALOG.parent/'include'),HERE/'electrical_export.cpp','-o',work/'export']),
              ('export',[work/'export',work/'electrical']),
              ('bindings',[sys.executable,HERE/'shared_analog.py','--electrical',work/'electrical/electrical.json','--output',work/'analog.json']),
              ('leaves',[python,HERE/'shared_leaf_layout.py','--plan',work/'analog.json','--pdk',args.pdk.resolve(),'--output',work/'leaves']),
              ('floorplan-input',[sys.executable,HERE/'shared_floorplan.py','--plan',work/'analog.json','--leaves',work/'leaves','--work',work]),
              ('physical-contract',[python,HERE/'tests/shared_physical.py','--plan',work/'analog.json','--leaves',work/'leaves'])]
    for name,command in commands:
        print(name,flush=True)
        with (work/('prepare-'+name+'.log')).open('w') as log:
            execute([str(x) for x in command],check=True,timeout=600,stdout=log,stderr=-2,memory_exclusive=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--pdk',type=Path,default=ANALOG/'pdk/IHPSG13G2')
    parser.add_argument('--cxx',default='c++')
    configure_cleanup()
    run(parser.parse_args())
