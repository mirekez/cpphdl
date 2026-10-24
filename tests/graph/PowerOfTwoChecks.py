"""Check arithmetic strength reduction after hierarchy constants resolve."""
import argparse,subprocess,tempfile
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-power-two-') as temporary:
    binary=Path(temporary)/'check'
    toolchain = TestToolchain(a.cxx, None, Path(temporary))
    subprocess.run([a.cxx,'-std=c++17','-O1','-DCPPHDL_GRAPH_CORE_ONLY','-I'+str(root/'include'),str(Path(__file__).with_suffix('.cc').with_name('PowerOfTwo.cc')),'-o',binary],env=toolchain.env,check=True)
    subprocess.run([binary],env=toolchain.env,check=True)
