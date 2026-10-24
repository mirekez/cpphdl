"""Output settling must skip next-state chunks and retain validation cones."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
import tempfile

p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-settle-') as temporary:
    work=Path(temporary)
    toolchain = TestToolchain(a.cxx, None, work)
    def run(args):
        r=subprocess.run(toolchain.command(args),env=toolchain.env,capture_output=True,text=True,timeout=240)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return r.stdout
    (work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
Graph graph(bool checked){
 Graph g;g.clockContract=ClockContract::RisingEdgeStep;
 auto input=[&](const char* name,unsigned width){auto v=g.wire(width,name,"input");g.ports.push_back({name,v,true});return v;};
 auto data=input("data",64),enable=input("enable",1),reset=input("reset",1);
 auto chain=[&](Value v,unsigned seed){for(unsigned r=0;r<96;++r){v=g.binary("add",v,constant(r*7919+seed,64),64);v=g.binary("xor",v,g.binary("shr",v,constant(13,64),64),64);}return v;};
 for(unsigned b=0;b<8;++b){auto state=g.wire(64,"state","state");
  auto next=chain(g.binary("xor",state,data,64),b);
  g.states.push_back({state,g.mux(reset,constant(0,64),g.mux(enable,next,state)),{1}});
  g.ports.push_back({"out"+std::to_string(b),state,false});
 }
 if(checked){g.memories.push_back({"ram",64,16});
  auto low=g.binary("and",chain(data,100),constant(15,64),64);
  auto address=g.mux(slice(data,63,1),constant(99,64),low);
  // The address cone has no output or register consumer. It must still run
  // during eval(false), where a live combinational access can throw.
  g.memoryAccesses.push_back({0,address,enable,false});
 }
 return g;
}
int main(int argc,char** argv){
 for(bool checked:{false,true})for(unsigned mode=0;mode<4;++mode){
  if(checked&&mode==3)continue;
  auto g=graph(checked);auto name=std::string(checked?"checked":"plain")+std::to_string(mode);
  g.emit(std::string(argv[1])+name+".h",mode?32:0,mode>=2?6:1,false,false,mode!=0&&!checked,false,mode==3);
 }
}
''')
    run([a.cxx,'-O1','-std=c++23','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit'])
    run([work/'emit',str(work)+'/'])
    includes=''
    for name in ('plain0','plain1','plain2','plain3','checked0','checked1','checked2'):
        includes+=f'#define cpphdl_native {name}\n#include "{name}.h"\n#undef cpphdl_native\n'
    (work/'check.cpp').write_text(includes+r'''
#include <cassert>
#include <cstdio>
template<class M>auto outputs(const M& m){return std::array{m.out0,m.out1,m.out2,m.out3,m.out4,m.out5,m.out6,m.out7};}
template<class Reference,class Model>void check(bool checked){
 Reference reference;Model model;std::array<uint64_t,8> oracle{};
 for(unsigned i=0;i<1024;++i){
  uint64_t data=i*0x9e3779b97f4a7c15ull;bool enable=i%7!=0,reset=i%101==0;
  auto input=[&](auto& m){m.data={uint32_t(data),uint32_t(data>>32)};m.enable[0]=enable;m.reset[0]=reset;};
  input(reference);input(model);
  for(bool commit:{false,true,false}){
   bool refError=false,error=false;
   try{reference.eval(commit);}catch(const std::out_of_range&){refError=true;}
   try{model.eval(commit);}catch(const std::out_of_range&){error=true;}
   assert(error==refError&&error==(checked&&enable&&bool(data>>63)));
   assert(outputs(model)==outputs(reference));
   if(!checked&&commit){for(unsigned b=0;b<8;++b){
    auto v=oracle[b]^data;for(unsigned r=0;r<96;++r){v+=r*7919+b;v^=v>>13;}
    if(reset)oracle[b]=0;else if(enable)oracle[b]=v;
   }}
  }
  if(!checked){auto out=outputs(model);for(unsigned b=0;b<8;++b)assert((uint64_t(out[b][0])|(uint64_t(out[b][1])<<32))==oracle[b]);}
  if(i%31==0){auto copy=model;auto refCopy=reference;copy.data={0,0};refCopy.data={0,0};copy.step();refCopy.step();assert(outputs(copy)==outputs(refCopy));}
 }
}
int main(){
 static_assert(plain1::Model::__cpphdl_settle_chunks==0);
 static_assert(plain2::Model::__cpphdl_settle_chunks==0);
 static_assert(plain3::Model::__cpphdl_settle_chunks==0&&plain3::Model::__cpphdl_delta_memoization);
 static_assert(checked1::Model::__cpphdl_settle_chunks>0);
 static_assert(checked2::Model::__cpphdl_settle_chunks>0);
 plain1::Model p;plain2::Model q;p.eval(false);q.eval(false);
 assert(!p.__cpphdl_cache_valid_0&&!q.__cpphdl_cache_valid_0);
 p.eval(true);q.eval(true);assert(p.__cpphdl_cache_valid_0&&q.__cpphdl_cache_valid_0);
 check<plain0::Model,plain1::Model>(false);check<plain0::Model,plain2::Model>(false);
 check<plain0::Model,plain3::Model>(false);
 check<checked0::Model,checked1::Model>(true);check<checked0::Model,checked2::Model>(true);
 std::puts("PASS native settling: next-state chunks skipped, cache first-use preserved, delta invalidation, scalar oracle, validation-only RAM cones, reset and copies; 15360 phases");
}
''')
    run([a.cxx,'-O1','-std=c++23','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',
         '-I'+str(root/'include'),work/'check.cpp','-o',work/'check'])
    print(run([work/'check']),end='')
