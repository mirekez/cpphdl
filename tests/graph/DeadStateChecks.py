"""Compare native pruning with models that expose every register as an output."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cxx', required=True)
args = p.parse_args()
root = Path(__file__).resolve().parents[2]

def run(argv):
    result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout

with tempfile.TemporaryDirectory(prefix='cpphdl-dead-state-') as temporary:
    work = Path(temporary)
    (work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
#include <cassert>
using namespace cpphdl::graph;
Value input(Graph& g,const char* name,unsigned width) {
 auto bits=g.wire(width,name,"input");g.ports.push_back({name,bits,true});return bits;
}
void dead(Graph& g) {
 auto a=g.wire(65,"dead_a","state"),b=g.wire(65,"dead_b","state");
 g.states.push_back({a,g.binary("add",b,constant(1,65),65),{1},0});
 g.states.push_back({b,g.binary("xor",a,constant(9,65),65),{1},0});
}
void emit(Graph g,const std::string& path,unsigned chunk,bool reference) {
 if(reference) for(size_t i=0;i<g.states.size();++i)
  g.ports.push_back({"keep"+std::to_string(i),g.states[i].bits,false});
 auto before=g.states.size();g.emit(path,chunk);
 assert(g.states.size()==before-(reference?0:2));
 for(const auto& n:g.nodes)if(!reference)assert(n.name.find("dead_")==std::string::npos);
 // Emission and compaction remain repeatable after IDs have changed.
 assert(g.pruneUnusedState()==0);
}
Graph sequential() {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks={{"clock",100},{"slow",33}};
 auto data=input(g,"data",8),enable=input(g,"enable",1),reset=input(g,"reset",1);
 input(g,"unused_input",3);
 auto stage=g.wire(8,"stage","state"),pipe=g.wire(8,"pipe","state");
 auto result=g.wire(8,"result","state"),gate=g.wire(1,"gate","state");
 auto falling=g.wire(8,"falling","state"),wide=g.wire(129,"wide","state");
 auto lowWord=g.wire(8,"wide_group_dependency","state");
 g.states.push_back({stage,g.mux(enable,data,stage),{1},0,false,reset,constant(3,8)});
 g.states.push_back({pipe,g.mux(gate,stage,pipe),{1},0,false,reset,constant(5,8)});
 g.states.push_back({result,pipe,{1},0,false,reset,constant(7,8)});
 g.states.push_back({gate,g.unary("not",gate),{1},0});
 g.states.push_back({falling,data,{1},1,true});
 g.states.push_back({lowWord,data,{1},0});
 auto next=resize(lowWord,129);next[128]=pipe[0];
 g.states.push_back({wide,next,{1},1});
 auto alias=g.wire(8,"output_alias");g.connect(alias,result);
 g.ports.push_back({"result",alias,false});g.ports.push_back({"high",slice(wide,128,1),false});
 g.memories.push_back({"ram",8,4});
 // Falling-edge state has no output consumer, but is live through RAM writes.
 g.memoryWrites.push_back({0,constant(0,2),falling,gate,0});
 dead(g);return g;
}
Graph bounds() {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks={{"clock",100}};
 auto data=input(g,"data",2),enable=input(g,"enable",1);
 auto address=g.wire(2,"address","state"),guard=g.wire(1,"guard","state");
 g.states.push_back({address,data,{1},0});g.states.push_back({guard,enable,{1},0});
 g.memories.push_back({"unread",8,2});
 // There is no read node or output: the enabled bounds error is observable.
 g.memoryAccesses.push_back({0,address,guard,false});
 dead(g);return g;
}
Graph effects() {
 Graph g;g.clockContract=ClockContract::ExplicitEvents;
 auto enable=input(g,"enable",1),guard=g.wire(1,"guard","state");
 auto capture=input(g,"capture",1);
 auto result=g.wire(32,"saved","state");
 auto trigger=g.wire(1,"trigger_only","state");
 g.states.push_back({guard,enable,{1}});
 g.states.push_back({trigger,capture,{1}});
 auto first=g.add("host_random",32,{}, {},guard);
 // Discarded result still must execute, after the first call, under its guard.
 g.add("host_random",32,first,{},{1});
 g.states.push_back({result,first,trigger});g.ports.push_back({"result",result,false});
 dead(g);for(auto& s:g.states)s.clock=-1;
 return g;
}
int main(int argc,char**argv) {
 std::string dir=argv[1];
 auto g=sequential();auto original=g.states.size();
 // Partition serialization must not discard any state before final linking.
 g.writeCpp(dir+"/partition.graph");assert(g.states.size()==original);
 for(unsigned chunk:{0,3})emit(sequential(),dir+"/seq"+std::to_string(chunk)+".h",chunk,false);
 emit(sequential(),dir+"/seq_ref.h",0,true);
 emit(bounds(),dir+"/bounds.h",3,false);emit(bounds(),dir+"/bounds_ref.h",0,true);
 emit(effects(),dir+"/effects.h",0,false);emit(effects(),dir+"/effects_ref.h",0,true);
 // The all-unused case has no roots and must remove a mutually dependent loop.
 Graph empty;empty.clockContract=ClockContract::NamedEdges;empty.clocks={{"clock",100}};dead(empty);
 empty.emit(dir+"/empty.h",3);assert(empty.states.empty() && empty.nodes.empty());
 // Streaming bindings are outside this pass's final-native contract.
 Graph streaming=sequential();streaming.pipelines.push_back({});
 assert(streaming.pruneUnusedState()==0 && streaming.states.size()==original);
 // Even unused invalid clocks/reset/event histories must still be rejected.
 auto rejected=[&](Graph bad) {
  bool threw=false;try{bad.emit(dir+"/invalid.h");}catch(const std::runtime_error&){threw=true;}
  assert(threw);
 };
 auto bad=sequential();bad.states.back().clock=99;rejected(bad);
 bad=sequential();bad.states.back().reset={1};bad.states.back().resetValue=constant(0,65);rejected(bad);
 bad=effects();auto history=bad.wire(1,"unused_history","state");
 bad.nodes[Graph::owner(history[0])].name="event history";
 bad.states.push_back({history,bad.states[0].bits,{1}});rejected(bad);
 // Dead combinational cycles cannot disappear before normal graph validation.
 bad=sequential();auto cycle=bad.wire(1,"cycle");
 auto invert=bad.unary("not",cycle);bad.connect(cycle,invert);
 bad.states.back().next=resize(cycle,65);rejected(bad);
}
''')
    run([args.cxx, '-std=c++23', '-O1', '-I'+str(root/'include'),
         work/'emit.cpp', '-o', work/'emit'])
    run([work/'emit', work])
    (work/'check.cpp').write_text(r'''
#define cpphdl_native seq_ref
#include "seq_ref.h"
#undef cpphdl_native
#define cpphdl_native seq0
#include "seq0.h"
#undef cpphdl_native
#define cpphdl_native seq3
#include "seq3.h"
#undef cpphdl_native
#define cpphdl_native bounds_ref
#include "bounds_ref.h"
#undef cpphdl_native
#define cpphdl_native bounds
#include "bounds.h"
#undef cpphdl_native
#define cpphdl_native effects_ref
#include "effects_ref.h"
#undef cpphdl_native
#define cpphdl_native effects
#include "effects.h"
#undef cpphdl_native
#include "empty.h"
#include <cassert>
#include <cstdio>
// Both models start each evaluation with the same external RNG state.
static unsigned calls=0;
extern "C" long random() noexcept {return 0x1234+17*++calls;}
template<class M> void inputs(M& m,unsigned i) {
 m.data[0]=(i*7919)%256;m.enable[0]=(i/7)%2;m.reset[0]=i%59==0;
 m.clock=(i/3)%2;m.slow=(i/5)%2;m.unused_input[0]=i%8;
}
template<class M> bool same(const seq_ref::Model& r,const M& m) {
 return r.result==m.result && r.high==m.high && r.memory0==m.memory0;
}
template<class M> bool evalBounds(M& m,bool commit) {
 try{m.eval(commit);return false;}catch(const std::out_of_range&){return true;}
}
int main() {
 seq_ref::Model r;seq0::Model a;seq3::Model b;
 unsigned changes=0;uint32_t last=0;
 for(unsigned i=0;i<4096;++i) {
  inputs(r,i);inputs(a,i);inputs(b,i);
  for(bool commit:{false,true,false}) {
   r.eval(commit);a.eval(commit);b.eval(commit);assert(same(r,a)&&same(r,b));
   changes+=r.result[0]!=last;last=r.result[0];
  }
 }
 assert(changes>100);
 unsigned errors=0;
 for(unsigned i=0;i<16;++i) {
  bounds_ref::Model r;bounds::Model m;
  r.data[0]=m.data[0]=i%4;r.enable[0]=m.enable[0]=(i/4)%2;
  r.clock=m.clock=true;
  assert(evalBounds(r,true)==evalBounds(m,true));
  bool expected=(i%4>=2 && (i/4)%2);
  bool error=evalBounds(m,false);assert(error==expected && error==evalBounds(r,false));errors+=error;
 }
 assert(errors==4);
 effects_ref::Model er;effects::Model em;unsigned total=0;
 for(unsigned i=0;i<1024;++i) {
  er.enable[0]=em.enable[0]=(i/3)%2;
  er.capture[0]=em.capture[0]=(i/5)%2;
  for(bool commit:{false,true,false}) {
   calls=0;er.eval(commit);unsigned expected=calls;
   calls=0;em.eval(commit);assert(calls==expected && em.result==er.result);
   assert(commit ? (calls==1 || calls==2) : calls==0);total+=calls;
  }
 }
 assert(total>1024);
 cpphdl_native::Model empty;empty.clock=true;empty.step();
 std::puts("dead state: sequential cones, wide groups, RAM effects/bounds, clocks/resets, host calls, validation and partition preservation PASS");
}
''')
    run([args.cxx, '-std=c++23', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
         '-fno-omit-frame-pointer', work/'check.cpp', '-o', work/'check'])
    print(run([work/'check']), end='')
