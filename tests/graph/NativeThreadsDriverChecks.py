"""Exercise the public native-graph thread option and post-worker exceptions."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cpphdl', required=True)
p.add_argument('--cxx', required=True)
args = p.parse_args()

def run(command, success=True):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=180)
    if (result.returncode == 0) != success:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout

with tempfile.TemporaryDirectory(prefix='cpphdl-thread-driver-') as tmp:
    work = Path(tmp)
    source = work/'graph.cpp'
    source.write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::RisingEdgeStep;
 auto input=[&](const char* name,unsigned width){auto bits=g.wire(width,name,"input");g.ports.push_back({name,bits,true});return bits;};
 auto address=input("address",8),enable=input("enable",1),data=input("data",64);
 g.memories.push_back({"ram",64,2});
 auto read=g.add("memory_read",64,address,constant(0,64),constant(0,64));
 for(unsigned branch=0;branch<4;++branch) {
  auto state=g.wire(64,"state"+std::to_string(branch),"state");
  auto value=g.binary("xor",data,state,64);
  for(unsigned i=0;i<128;++i) {
   value=g.binary("add",value,constant(13*i+branch+1,64),64);
   value=g.binary("xor",value,constant(29*i+branch,64),64);
  }
  g.states.push_back({state,value,enable});
  // Ordered updates to one register must stay together. A disabled later
  // update must not undo an enabled earlier one; both read the old state.
  if(branch<2) g.states.push_back({state,g.binary("add",state,data,64),slice(data,0,1)});
  g.ports.push_back({"out"+std::to_string(branch),g.binary("xor",state,read,64),false});
 }
 g.memoryAccesses.push_back({0,address,enable,false});
 g.memoryWrites.push_back({0,address,data,enable});
 g.emit(argv[1]);
}
''')
    runner = work/'runner.cpp'
    runner.write_text(r'''
#include <cassert>
#include <cstdio>
#if EXPECT_THREADS > 1
static_assert(cpphdl_native::Model::__cpphdl_thread_count==EXPECT_THREADS);
#endif
int main() {
 cpphdl_native::Model m;uint64_t checksum=0;
 for(unsigned i=0;i<256;++i) {
  m.data={i*17u,i*31u};m.address[0]=i%2;m.enable[0]=1;m.step();
  checksum=checksum*131+m.out0[0]+m.out1[1]+m.out2[0]+m.out3[1];
  auto outputs=std::array{m.out0,m.out1,m.out2,m.out3};
  auto memory=m.memory0;
  m.address[0]=255;
  bool threw=false;try{m.eval(true);}catch(const std::out_of_range&){threw=true;}
  assert(threw);
  assert((std::array{m.out0,m.out1,m.out2,m.out3}==outputs));
  assert(m.memory0==memory);
  // A failed validation happens after workers finish and must not poison the
  // executor or commit a write. A disabled invalid access stays nonthrowing.
  m.enable[0]=0;m.eval(false);m.address[0]=i%2;m.enable[0]=1;m.eval(false);
  // Copy after both odd and even commit counts, including a discarded bank
  // left by failed validation. Each copy must own all simulation storage.
  auto copy=m;copy.data={i*101u,i*211u};copy.step();
  checksum=checksum*131+copy.out0[0]+copy.out1[1]+copy.out2[0]+copy.out3[1];
 }
 std::printf("threads checksum %llu\n",(unsigned long long)checksum);
}
''')
    outputs = []
    for threads in (1, 4):
        out = work/f'threads-{threads}'
        run([args.cpphdl, '--native-graph', '--optimize-threads='+str(threads),
             '--cxx', args.cxx, '--output', out, '--runner', runner, source, '--',
             '-DEXPECT_THREADS='+str(threads), '-fsanitize=undefined', '-fno-sanitize-recover=all'])
        manifest = json.loads((out/'manifest.json').read_text())
        assert manifest['optimize_threads'] == threads and manifest['status'] == 'complete'
        outputs.append(run([out/'run']))
    assert outputs[0] == outputs[1]
    for value in ('0', '-1', '257', '2junk'):
        out = work/('invalid'+value)
        run([args.cpphdl, '--native-graph', '--optimize-threads', value,
             '--cxx', args.cxx, '--output', out, source], success=False)
        assert not out.exists()
    host = work/'host.cpp'
    host.write_text(r'''
#include <cpphdl_graph_native.h>
int main(int argc,char**argv) {
 cpphdl::graph::Graph g;g.add("host_random",32,{},{},{1});g.emit(argv[1]);
}
''')
    out = work/'host-effect'
    run([args.cpphdl, '--native-graph', '--optimize-threads=2', '--cxx', args.cxx,
         '--output', out, host], success=False)
    assert not (out/'model.h').exists()
    assert 'explicit host boundaries' in (out/'lower.log').read_text()
    print('native thread driver: options, deterministic outputs, exception recovery and host-boundary rejection PASS')
