#!/usr/bin/env python3
"""Re-emit an existing graph using current codegen; preserve previous runners."""
import argparse
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--source', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
args = p.parse_args()
src, out = args.source.resolve(), args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
root = Path('/home/me/cpphdl')
cxx = root/'.conda/bin/clang++'
def run(argv):
    subprocess.run(list(map(str, argv)), check=True)
(out/'emit.cpp').write_text('''#include <cpphdl_graph_native.h>
int main(int argc,char**argv) {
 std::ifstream f(argv[1]);std::ostringstream s;s<<f.rdbuf();cpphdl::graph::Graph g;g.load(s.str().c_str());
 g.emit(argv[2],512);
}
''')
run([cxx,'-std=c++23','-O2','-I'+str(root/'include'),out/'emit.cpp','-o',out/'emit'])
run([out/'emit',src/'model.h.graph',out/'model.h'])
(out/'model.h.graph').symlink_to(src/'model.h.graph')
for name in ('runner.cpp', 'hosts.json'):
    shutil.copy2(src/name,out/name)
(out/'CMakeLists.txt').write_text((src/'CMakeLists.txt').read_text().replace(str(src),str(out)))
run([root/'.conda/bin/cmake','-S',out,'-B',out/'cmake','-DCMAKE_CXX_COMPILER='+str(cxx)])
run([root/'.conda/bin/cmake','--build',out/'cmake','--parallel','1'])
