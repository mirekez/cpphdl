"""Outer parallel graph evaluation: exact clocks, isolation and failure draining."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
import tempfile

p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-native-batch-') as temporary:
    work=Path(temporary)
    toolchain = TestToolchain(a.cxx, None, work)
    def run(args):
        result=subprocess.run(toolchain.command(args),env=toolchain.env,capture_output=True,text=True,timeout=180)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        return result.stdout
    (work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc,char** argv){
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clk",100});
 auto input=[&](const char* name,unsigned width){auto v=g.wire(width,name,"input");g.ports.push_back({name,v,true});return v;};
 auto data=input("data",64),reset=input("reset",1);
 for(unsigned branch=0;branch<8;++branch){
  auto state=g.wire(64,"state","state"),next=g.binary("xor",state,data,64);
  for(unsigned i=0;i<16;++i)next=g.binary("add",g.binary("xor",next,constant(i*71+branch,64),64),data,64);
  g.states.push_back({state,next,Value{1}});auto& s=g.states.back();
  s.clock=0;s.falling=branch%2;s.reset=reset;s.resetValue=constant(17+branch,64);
  g.ports.push_back({"out"+std::to_string(branch),state,false});
 }
 g.emit(std::string(argv[1])+"reference.h",0,1);
 g.emit(std::string(argv[1])+"parallel.h",32,6,false,false,true);
}
''')
    run([a.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit'])
    run([work/'emit',str(work)+'/'])
    (work/'check.cpp').write_text(r'''
#define cpphdl_native reference
#include "reference.h"
#undef cpphdl_native
#define cpphdl_native parallel
#include "parallel.h"
#undef cpphdl_native
#include <cpphdl_graph_batch.h>
#include <cassert>
#include <barrier>
#include <cstdio>
using cpphdl::graph_runtime::Batch;
template<class M> auto outputs(const M& m){return std::array{m.out0,m.out1,m.out2,m.out3,m.out4,m.out5,m.out6,m.out7};}
int main(){
 auto executor=std::make_shared<cpphdl::graph_runtime::Threads>(6,1);Batch batch(6,executor);
 std::array<reference::Model,11> refs;
 std::array<parallel::Model,11> models;
 for(auto& model:models){model.__cpphdl_threads=executor;model.__cpphdl_dispatch_policy=cpphdl::graph_runtime::DispatchPolicy::Serial;}
 for(unsigned phase=0;phase<1024;++phase){
  for(unsigned i=0;i<models.size();++i){
   auto inputs=[&](auto& m){m.clk=phase%2;m.reset[0]=phase%97<2;m.data={phase*7919+i*1237,0x80000000u+i*991+phase};};
   inputs(refs[i]);inputs(models[i]);refs[i].step();
  }
  auto schedule=phase%2?Batch::Schedule::Static:Batch::Schedule::Dynamic;
  batch.run(models.size(),[&](std::size_t i){models[i].step();},schedule,1+phase%5);
  for(unsigned i=0;i<models.size();++i)assert(outputs(models[i])==outputs(refs[i]));
  if(phase%31==0){auto copy=models[3];copy.step();auto reference=refs[3];reference.step();assert(outputs(copy)==outputs(reference));}
 }
 // This handshake requires six jobs to be simultaneously runnable. It cannot
 // pass if the outer scheduler serializes the independent graph transactions.
 std::barrier rendezvous(6);
 for(auto schedule:{Batch::Schedule::Static,Batch::Schedule::Dynamic})
  batch.run(6,[&](std::size_t){rendezvous.arrive_and_wait();},schedule);
 Batch single(1);unsigned singleCount=0;single.run(7,[&](std::size_t){++singleCount;});assert(singleCount==7);
 for(auto schedule:{Batch::Schedule::Static,Batch::Schedule::Dynamic}){
  std::array<unsigned,17> visits{};bool threw=false;
  try{batch.run(visits.size(),[&](std::size_t i){++visits[i];if(i==5)throw std::runtime_error("job failed");},schedule,3);}
  catch(const std::runtime_error&){threw=true;}
  assert(threw);for(auto count:visits)assert(count==1);
  batch.run(visits.size(),[&](std::size_t i){++visits[i];},schedule,99);
  for(auto count:visits)assert(count==2);
 }
 batch.run(0,[](std::size_t){assert(false);});
 bool rejected=false;try{batch.run(1,[](std::size_t){},Batch::Schedule::Static,0);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 for(unsigned n:{0u,257u}){rejected=false;try{Batch invalid(n);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
 // Independent callers may share the executor; neither loses jobs or errors.
 std::array<unsigned,101> left{},right{};
 std::thread other([&]{for(unsigned r=0;r<17;++r)batch.run(left.size(),[&](std::size_t i){++left[i];});});
 for(unsigned r=0;r<19;++r)batch.run(right.size(),[&](std::size_t i){++right[i];},Batch::Schedule::Dynamic,7);
 other.join();for(auto n:left)assert(n==17);for(auto n:right)assert(n==19);
 std::puts("PASS native batch: 11 independent clocked graphs, 11264 model phases, both edges/reset/copies, static/dynamic, six simultaneous jobs, errors drained and concurrent callers");
}
''')
    run([a.cxx,'-std=c++23','-O1','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',
         '-I'+str(root/'include'),work/'check.cpp','-o',work/'check'])
    print(run([work/'check']),end='')
