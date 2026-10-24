"""Check memoized pure native chunks against uncached evaluation."""
import argparse
import re
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
import tempfile

p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
work=Path(tempfile.mkdtemp(prefix='cpphdl-native-memoization-'))
toolchain = TestToolchain(a.cxx, None, work)
def run(args):
    result=subprocess.run(toolchain.command(args),env=toolchain.env,text=True,capture_output=True,timeout=240)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return result.stdout

(work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
#include <cassert>
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clock",100});
 auto input=[&](const char* n,unsigned width){auto v=g.wire(width,n,"input");g.ports.push_back({n,v,true});return v;};
 auto data=input("data",64),enable=input("enable",1),reset=input("reset",1),address=input("address",2);
 auto wide=input("wide",4096);
 g.memories.push_back({"rom",64,4,{11,23,37,59}});
 auto rom=g.add("memory_read",64,address,constant(0,64),constant(0,64));
 auto fixed=g.add("state",64,constant(0xa735800012345678ull,64),{}, {},"fixed");
 g.ports.push_back({"fixed",fixed,false});
 g.pipelines.push_back({"retained_metadata"});
 Value packed;
 for(unsigned lane=0;lane<12;++lane) {
  auto state=g.wire(64,"state"+std::to_string(lane),"state");auto value=g.binary("xor",data,state,64);
  for(unsigned word=0;word<64;++word)
   value=g.binary("add",value,slice(wide,word*64,64),64);
  // Model the obsolete per-bit nodes left after register-word packing.
  for(unsigned bit=0;bit<64;++bit){auto alias=g.add("state",1,constant(0,1),{}, {},"old");g.connect(alias,{state[bit]});}
  // Twelve lanes still exceed the 4096-node adaptive dispatch threshold.
  for(unsigned i=0;i<180;++i)value=g.binary("xor",g.binary("add",value,rom,64),constant(i+lane*101,64),64);
  auto next=g.mux(enable,value,state);
  if(lane&2) {
   next.clear();
   for(unsigned bit=0;bit<64;++bit)
    next.push_back(g.mux(enable,Value{value[(bit*13+7)%64]},Value{state[bit]})[0]);
  }
  g.states.push_back({state,next,{1},0,bool(lane&1),reset,constant(lane,64)});
  packed.insert(packed.end(),value.begin(),value.end());
  g.ports.push_back({"out"+std::to_string(lane),value,false});
  g.ports.push_back({"state_out"+std::to_string(lane),state,false});
 }
 packed.insert(packed.end(),fixed.begin(),fixed.begin()+17);g.ports.push_back({"packed",packed,false});
 {
  auto bad=g;bad.add("host_random",32);
  bool rejected=false;try{bad.emit(std::string(argv[1])+"bad.h",7,6,false,false,true);}catch(const std::runtime_error&){rejected=true;}
  assert(rejected);
 }
 g.emit(std::string(argv[1])+"plain.h",7,6);
 bool rejected=false;try{g.emit(std::string(argv[1])+"invalid.h",7,6,false,false,false,true);}catch(const std::runtime_error&){rejected=true;}
 assert(rejected);
 g.emit(std::string(argv[1])+"cached_only.h",7,6,false,false,true);
 g.emit(std::string(argv[1])+"cached.h",7,6,false,false,true,true);
 g.emit(std::string(argv[1])+"cached_one.h",19,1,false,false,true,true);
 g.emit(std::string(argv[1])+"cached_wide.h",512,6,false,false,true,true);
 g.emit(std::string(argv[1])+"delta.h",19,6,false,false,true,true,true);
 g.emit(std::string(argv[1])+"delta_one.h",17,1,false,false,true,false,true);
 auto owned=g;owned.attributes.push_back({"","native.parallel_roots","1"});
 owned.attributes.push_back({"","native.cache_state_roots","1"});
 owned.emit(std::string(argv[1])+"delta_owned.h",19,6,false,false,true,true,true);
}
''')
run([a.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit'])
run([work/'emit',str(work)+'/'])
for name in ('plain.h','cached_only.h','cached.h','cached_one.h','cached_wide.h','delta.h','delta_one.h','delta_owned.h'):
    fields=re.findall(r'uint(?:8|16|32|64)_t state\d+ =', (work/name).read_text())
    if len(fields)!=13:raise RuntimeError(f'{name}: expected 12 written and one fixed live state word, got {len(fields)}')
if '__cpphdl_key{{' not in (work/'cached_wide.h').read_text():
    raise RuntimeError('Wide-key array comparison was not exercised')
# Compile each generated model separately: a single translation unit containing
# all variants exhausts memory on small CI machines before it can run the oracle.
models = ('plain', 'cached_only', 'cached', 'cached_one', 'cached_wide',
          'delta', 'delta_one', 'delta_owned')
(work/'adapter.h').write_text(r'''
#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <cpphdl_graph_threads.h>
using Policy = cpphdl::graph_runtime::DispatchPolicy;
struct Model {
 virtual ~Model() = default;
 virtual void inputs(unsigned, uint64_t) = 0;
 virtual void eval(bool) = 0;
 virtual void step() = 0;
 virtual void policy(Policy) = 0;
 virtual bool serial() const = 0;
 virtual std::vector<uint32_t> outputs() const = 0;
 virtual std::unique_ptr<Model> copy() const = 0;
};
''' + '\n'.join('std::unique_ptr<Model> make_'+name+'();' for name in models))
objects=[]
for name in models:
    assertions = ''
    if name in ('delta','delta_one'):
        assertions += 'static_assert(M::__cpphdl_delta_memoization);\n'
    if name in ('plain','cached_only'):
        assertions += 'static_assert(M::__cpphdl_cached_next_words==0);\n'
    if name in ('cached','cached_one'):
        assertions += 'static_assert(M::__cpphdl_cached_next_words>0);\n'
    members=[member for i in range(12) for member in (f'out{i}',f'state_out{i}')]
    members+=['packed','fixed']
    output_code=''.join('r.insert(r.end(),m.'+member+'.begin(),m.'+member+'.end());' for member in members)
    adaptive = name not in ('plain', 'cached_one', 'delta_one')
    policy_code='m.__cpphdl_dispatch_policy=p;' if adaptive else ''
    serial_code='m.__cpphdl_serial_dispatch' if adaptive else 'true'
    phase_code=r'''
 auto other=m.__cpphdl_serial_by_phase[!commit];
 m.eval(commit);
 assert(m.__cpphdl_serial_by_phase[!commit]==other);
 assert(m.__cpphdl_serial_by_phase[commit]==m.__cpphdl_serial_dispatch);
'''
    if not adaptive:phase_code='m.eval(commit);'
    (work/(name+'.cpp')).write_text(
        '#include "adapter.h"\n#define cpphdl_native model_'+name+'\n#include "'+name+'.h"\n#undef cpphdl_native\n'
        'namespace { using M=model_'+name+'::Model;\n'+assertions+r'''
class Adapter : public Model {
 M m;
public:
 void inputs(unsigned i,uint64_t data) override {
  m.data={uint32_t(data),uint32_t(data>>32)};
  for(unsigned k=0;k<128;++k)m.wide[k]=uint32_t(data>>((k&1)*32))^(k*7919u)^(i/97);
  m.enable[0]=(i/73)%2;m.reset[0]=i%257==0;m.address[0]=(i/17)%4;m.clock=(i/3)%2;
 }
 void eval(bool commit) override {'''+phase_code+r'''}
 void step() override {m.step();}
 void policy(Policy p) override {'''+policy_code+r'''}
 bool serial() const override {return '''+serial_code+r''';}
 std::vector<uint32_t> outputs() const override {
  assert(m.fixed[0]==0x12345678u&&m.fixed[1]==0xa7358000u);
  std::vector<uint32_t> r;'''+output_code+r'''return r;
 }
 std::unique_ptr<Model> copy() const override {return std::make_unique<Adapter>(*this);}
};
}
std::unique_ptr<Model> make_'''+name+'(){return std::make_unique<Adapter>();}\n')
    obj=work/(name+'.o');objects.append(obj)
    run([a.cxx,'-std=c++23','-O1','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',
         '-I'+str(root/'include'),'-c',work/(name+'.cpp'),'-o',obj])
(work/'check.cpp').write_text(r'''
#include "adapter.h"
#include <cstdio>
int main(){
 for(auto entry:{std::pair{"auto",Policy::Automatic},std::pair{"serial",Policy::Serial},std::pair{"parallel",Policy::Parallel}}){
  setenv("CPPHDL_NATIVE_DISPATCH",entry.first,1);assert(cpphdl::graph_runtime::nativeDispatchPolicy()==entry.second);}
 setenv("CPPHDL_NATIVE_DISPATCH","invalid",1);bool invalid=false;
 try{cpphdl::graph_runtime::nativeDispatchPolicy();}catch(const std::invalid_argument&){invalid=true;}assert(invalid);
 unsetenv("CPPHDL_NATIVE_DISPATCH");assert(cpphdl::graph_runtime::nativeDispatchPolicy()==Policy::Automatic);
 std::vector<std::unique_ptr<Model>> models;
'''+'\n'.join('models.push_back(make_'+name+'());' for name in models)+r'''
 uint64_t random=42;bool serial=false,parallel=false;
 for(unsigned i=0;i<4096;++i){
  if(i%101==0){random^=random<<13;random^=random>>7;random^=random<<17;}
  for(unsigned index:{1u,5u,7u})models[index]->policy(i%3==0?Policy::Serial:(i%3==1?Policy::Parallel:Policy::Automatic));
  for(auto& m:models)m->inputs(i,random);
  for(bool commit:{false,true,false}){
   models[0]->eval(commit);auto reference=models[0]->outputs();
   for(unsigned j=1;j<models.size();++j){models[j]->eval(commit);assert(models[j]->outputs()==reference);}
   serial|=models[2]->serial();parallel|=!models[2]->serial();
  }
 }
 assert(serial&&parallel);
 std::vector<std::unique_ptr<Model>> copies;
 for(auto& m:models)copies.push_back(m->copy());
 for(unsigned i=0;i<256;++i){
  for(auto& m:copies){m->inputs(i,7919*i);m->step();}
  auto reference=copies[0]->outputs();
  for(auto& m:copies)assert(m->outputs()==reference);
 }
 std::puts("PASS native chunk memoization: 12288 phases, six/one lanes, wide/short keys, exact delta invalidation, adaptive serial/parallel dispatch, both edges, reset, ROM, copied state and rejection checks");
}
''')
run([a.cxx,'-std=c++23','-O1','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',
     '-I'+str(root/'include'),work/'check.cpp',*objects,'-o',work/'check'])
print(run([work/'check']),end='')
