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
    g.emit(argv[1]);
    g.attributes.push_back({"","native.quiet_events","1"});g.emit(argv[2],2);
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
print('native reset events: stopped/independent clocks, quiet timestamps and state-clock rejection PASS')
