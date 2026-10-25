"""Compare RAM caching and private root scans against an independent clock oracle."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', required=True)
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
args.work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=args.work) as directory:
    work = Path(directory).resolve()
    tools = TestToolchain(args.cxx, None, work)

    def run(command):
        result = subprocess.run(tools.command(command), env=tools.env,
                                text=True, capture_output=True, timeout=180)
        if result.returncode:
            raise RuntimeError(str(command) + '\n' + result.stdout + result.stderr)

    source = work / 'emit.cpp'
    source.write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc, char** argv) {
    Graph g; g.clockContract = ClockContract::NamedEdges;
    g.clocks.push_back({"clock", 100});
    auto input = [&](const char* name, unsigned width) {
        auto value = g.wire(width, name, "input"); g.ports.push_back({name, value, true}); return value;
    };
    auto data = input("data",32), address = input("address",4), enable = input("enable",1), reset = input("reset",1);
    g.memories.push_back({"ram",32,16});
    auto load = g.add("memory_read",32,address,constant(0,64),constant(0,64));
    g.ports.push_back({"read",load,false});
    for (unsigned i=0; i<4; ++i) {
        auto state = g.wire(32,"state"+std::to_string(i),"state");
        auto next = g.binary("add",g.binary("add",state,load,32),data,32);
        next = g.binary("add",next,constant(i,32),32);
        g.states.push_back({state,g.mux(reset,constant(0,32),g.mux(enable,next,state)),{1},0,false});
        g.ports.push_back({"state_out"+std::to_string(i),state,false});
    }
    g.memoryWrites.push_back({0,address,data,enable,0,false});
    const unsigned mode = std::stoul(argv[2]), threads = std::stoul(argv[3]);
    if (mode >= 3) g.attributes.push_back({"","native.parallel_roots","1"});
    if (mode == 4) g.attributes.push_back({"","native.cache_state_roots","1"});
    g.emit(argv[1],2,threads,false,false,mode>0,mode>0,mode>=2);
}
''')
    flags = [args.cxx, '-std=c++23', '-O1', '-I' + str(repo / 'include')]
    run([*flags, source, '-o', work / 'emit'])
    runner = work / 'run.cpp'
    runner.write_text(r'''
#include "model.h"
#include <cassert>
int main() {
    cpphdl_native::Model model;
    uint32_t ram[16]{}, state[4]{};
    bool previous=false;
    auto check = [&] {
        assert(model.read[0]==ram[model.address[0]]);
        assert(model.state_out0[0]==state[0] && model.state_out1[0]==state[1]);
        assert(model.state_out2[0]==state[2] && model.state_out3[0]==state[3]);
        for(unsigned a=0;a<16;++a) assert(model.memory0[a][0]==ram[a]);
    };
    for(unsigned i=0;i<64000;++i) {
        model.clock=(i/3)%2; model.enable[0]=(i/11)%2; model.reset[0]=i%193==0;
        model.data[0]=i*7919; model.address[0]=(i/29)%16;
        if(i%17==0) model.memory0[model.address[0]][0]=ram[model.address[0]]=i*13;
        model.eval(false); check();
        if(model.clock && !previous) {
            for(unsigned lane=0;lane<4;++lane)
                if(model.reset[0]) state[lane]=0;
                else if(model.enable[0]) state[lane]+=ram[model.address[0]]+model.data[0]+lane;
            if(model.enable[0]) ram[model.address[0]]=model.data[0];
        }
        model.step(); previous=model.clock; check();
        if(i%127==0) {
            auto copy=model; copy.eval(false);
            assert(copy.read==model.read && copy.state_out0==model.state_out0);
            copy.memory0[model.address[0]][0]^=0x1234; copy.eval(false);
            assert(copy.read[0]==(model.read[0]^0x1234));
            model.eval(false); check();
        }
    }
}
''')
    for mode, threads in ((0,1), (1,1), (1,2), (2,1), (2,2), (3,2), (4,2), (4,1)):
        run([work / 'emit', work / 'model.h', mode, threads])
        run([*flags, '-O2', '-pthread', runner, '-o', work / 'run'])
        run([work / 'run'])
        print(f'RAM oracle: mode={mode}, threads={threads}, 64000 transactions PASS', flush=True)
