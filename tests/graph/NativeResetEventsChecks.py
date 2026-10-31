"""Check native graph C++17 compatibility, asynchronous resets and quiet timestamps."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-reset-events-') as directory:
    work = Path(directory)
    tools = TestToolchain(args.cxx, None, work)
    def run(command):
        result = subprocess.run(tools.command(command), env=tools.env,
                                text=True, capture_output=True, timeout=120)
        if result.returncode:
            raise RuntimeError(str(command) + '\n' + result.stdout + result.stderr)
    source = work / 'emit.cpp'
    source.write_text(r'''
#include <cpphdl_graph_native.h>
#include <cassert>
using namespace cpphdl::graph;
int main(int argc,char**argv) {
    Graph g; g.clockContract=ClockContract::ExplicitEvents;
    auto input=[&](const char*name){auto v=g.wire(1,name,"input");g.ports.push_back({name,v,true});return v;};
    auto clk=input("clk"),slow=input("slow"),reset=input("reset");
    auto edge=[&](Value v,bool falling,const char* name) {
        auto prev=g.add("state",1,constant(falling,1),{},{},name);
        g.states.push_back({prev,v,{1}});
        if(falling)g.ports.push_back({"__cpphdl_async_reset_0",Value{v[0],prev[0]},false});
        return g.binary("and",falling?g.unary("not",v):v,falling?prev:g.unary("not",prev),1);
    };
    auto fastEdge=edge(clk,false,"event history"),slowEdge=edge(slow,false,"event history");
    auto reset_n=g.wire(1,"reset_n","state");
    g.states.push_back({reset_n,g.unary("not",reset),fastEdge});
    auto resetEdge=edge(reset_n,true,"async reset history");
    auto state=g.wire(8,"value","state");
    auto enable=g.binary("or",slowEdge,resetEdge,1);
    g.states.push_back({state,g.mux(reset_n,g.binary("add",state,constant(1,8),8),constant(0,8)),enable});
    g.ports.push_back({"value",state,false});
    // Independent state cones force the parallel variants to keep all lanes.
    for(unsigned b=0;b<8;++b) {
        auto s=g.wire(32,"lane_state"+std::to_string(b),"state"),next=s;
        for(unsigned j=0;j<16;++j)
            next=g.binary("mul",g.binary("add",next,constant(j+b+1,32),32),constant(3,32),32);
        g.states.push_back({s,next,fastEdge});
        g.ports.push_back({"out"+std::to_string(b),s,false});
    }
    auto original=g;
    g.emit(argv[1]);
    auto quiet=original;quiet.attributes.push_back({"","native.quiet_events","1"});quiet.emit(argv[2],2);
    for(unsigned n:{2u,4u,6u}) {
        auto parallel=original;parallel.attributes.push_back({"","native.quiet_events","1"});
        parallel.emit(std::string(argv[2])+std::to_string(n),16,n,false,false,true,false,true);
    }
    // State-derived clocks are still rejected, unlike labeled reset events.
    Graph bad; bad.clockContract=ClockContract::ExplicitEvents;
    auto derived=bad.wire(1,"derived","state");
    auto history=bad.add("state",1,{},{},{},"event history");
    bad.states.push_back({derived,bad.unary("not",derived),{1}});
    bad.states.push_back({history,derived,{1}});
    bad.ports.push_back({"out",history,false});
    bool rejected=false;
    try { bad.emit(std::string(argv[2])+".bad"); } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected);
}
''')
    flags = [args.cxx, '-std=c++17', '-O1', '-I' + str(repo / 'include')]
    run([*flags, source, '-o', work / 'emit'])
    run([work / 'emit', work / 'plain.h', work / 'quiet.h'])
    runner = work / 'run.cpp'
    runner.write_text(r'''
#define cpphdl_native plain
#include "plain.h"
#undef cpphdl_native
#include "quiet.h"
#include <cassert>
int main() {
    plain::Model a; cpphdl_native::Model b;
    unsigned expected=0; bool oldFast=false,oldSlow=false,reset_n=false;
    unsigned quiet=0;
    for(unsigned i=0;i<4096;++i) {
        bool fast=(i/2)%2,slow=(i/13)%2,reset=i%89<11;
        a.clk[0]=b.clk[0]=fast; a.slow[0]=b.slow[0]=slow; a.reset[0]=b.reset[0]=reset;
        if(slow&&!oldSlow)expected=reset_n?(expected+1)&255:0;
        if(fast&&!oldFast) { if(reset&&reset_n)expected=0; reset_n=!reset; }
        a.step(); b.eval(false);
        if(b.__cpphdl_advance_quiet_event()) { ++quiet; b.eval(false); } else b.step();
        assert(a.value[0]==expected && b.value[0]==expected);
        oldFast=fast;oldSlow=slow;
    }
    assert(quiet>100);
}
''')
    run([*flags, runner, '-o', work / 'run'])
    run([work / 'run'])
    parallel = work / 'parallel.cpp'
    parallel.write_text(r'''
#define cpphdl_native plain
#include "plain.h"
#undef cpphdl_native
#define cpphdl_native quiet2
#include "quiet.h2"
#undef cpphdl_native
#define cpphdl_native quiet4
#include "quiet.h4"
#undef cpphdl_native
#define cpphdl_native quiet6
#include "quiet.h6"
#undef cpphdl_native
#include <cassert>
template<class M> auto outputs(const M& m){return std::array{m.value,m.out0,m.out1,m.out2,m.out3,m.out4,m.out5,m.out6,m.out7};}
template<class M> void check(){
    plain::Model reference;M model;std::array<uint32_t,8> expected{};
    bool previousFast=false;unsigned quiet=0;
    for(unsigned i=0;i<8192;++i){
        // Extended stopped-clock intervals exercise cached bank transitions.
        bool fast=i%257<160?(i/2)%2:false,slow=(i/13)%2,reset=i%89<11;
        auto drive=[&](auto& m){m.clk[0]=fast;m.slow[0]=slow;m.reset[0]=reset;};
        drive(reference);drive(model);reference.step();model.eval(false);
        if(model.__cpphdl_advance_quiet_event()){++quiet;model.eval(false);}else model.step();
        if(fast&&!previousFast)for(unsigned b=0;b<8;++b)for(unsigned j=0;j<16;++j)expected[b]=(expected[b]+j+b+1)*3u;
        auto actual=outputs(model);assert(actual==outputs(reference));
        for(unsigned b=0;b<8;++b)assert(actual[b+1][0]==expected[b]);
        previousFast=fast;
    }
    assert(quiet>100);
}
int main(){
    static_assert(quiet2::Model::__cpphdl_thread_count==2);
    static_assert(quiet4::Model::__cpphdl_thread_count==4);
    static_assert(quiet6::Model::__cpphdl_thread_count==6);
    check<quiet2::Model>();check<quiet4::Model>();check<quiet6::Model>();
}
''')
    run([args.cxx, '-std=c++23', '-O1', '-pthread', '-fsanitize=undefined', '-fno-sanitize-recover=all',
         '-I' + str(repo / 'include'), parallel, '-o', work / 'parallel'])
    run([work / 'parallel'])
print('native reset events: stopped/independent clocks, quiet timestamps and state-clock rejection PASS')
