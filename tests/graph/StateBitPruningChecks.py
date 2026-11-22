"""Exercise final RTL bit-level state pruning on small sequential graphs."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]

source = r'''
#include "StatePruning.h"
#include <cassert>
#include <iostream>
using namespace cpphdl::graph;
using cpphdl::synth::pruneUnusedStateBits;
using cpphdl::synth::synchronousRead;

Value input(Graph& g,const char* name,unsigned width) {
 auto bits=g.wire(width,name,"input");g.ports.push_back({name,bits,true});return bits;
}
Graph base() {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks={{"clk",250000000}};return g;
}
void partial_alias_and_reset() {
 auto g=base();auto enable=input(g,"enable",1),reset=input(g,"reset",1);
 auto data=input(g,"data",65),s=g.wire(65,"payload","state");
 auto next=g.mux(enable,data,s);auto original=s;
 g.states.push_back({s,next,{1},0,false,reset,constant(1,65)});
 auto alias=g.wire(2,"result");g.connect(alias,{s[64],s[0]});
 g.ports.push_back({"result",alias,false});
 g.optimize();g.dependencyOrder();
 assert(pruneUnusedStateBits(g)==63);
 assert((g.states.size()==1 && g.states[0].bits==Value{original[0],original[64]}));
 assert((g.states[0].next.size()==2 && g.states[0].resetValue==Value{1,0}));
 assert(g.states[0].reset==reset && g.states[0].trigger==Value{1});
 for(unsigned i=1;i<64;++i)assert(g.resolve(original[i])==0);
 assert(g.resolve(original[0])==original[0]);
 assert(g.resolve(original[64])==original[64]);
 g.optimize();g.dependencyOrder();
}
void bitwise_and_future_cycle() {
 auto g=base();auto data=input(g,"data",8),mask=input(g,"mask",8);
 auto first=g.wire(8,"first","state"),second=g.wire(8,"second","state");
 g.states.push_back({first,data,{1},0});
 g.states.push_back({second,g.binary("xor",first,mask,8),{1},0});
 g.ports.push_back({"result",{second[5]},false});
 g.optimize();g.dependencyOrder();
 assert(pruneUnusedStateBits(g)==14);
 assert(g.states.size()==2 && g.states[0].bits==Value{first[5]});
 assert(g.states[1].bits==Value{second[5]});
 assert(g.resolve(first[4])==0 && g.resolve(second[4])==0);
 g.optimize();g.dependencyOrder();
}
void conservative_carry() {
 auto g=base();auto other=input(g,"other",8),s=g.wire(8,"carry","state");
 g.states.push_back({s,other,{1},0});
 auto sum=g.add("add",8,s,other);g.ports.push_back({"top",{sum[7]},false});
 g.optimize();g.dependencyOrder();
 assert(pruneUnusedStateBits(g)==0 && g.states[0].bits.size()==8);
}
void replacement_pin() {
 auto g=base();auto data=input(g,"data",8),s=g.wire(8,"pin_state","state");
 g.states.push_back({s,data,{1},0});
 g.replacements.push_back({"hard","HardBlock","",1,{{"operation_in",{s[3]}}}});
 g.optimize();g.dependencyOrder();
 assert(pruneUnusedStateBits(g)==7);
 assert(g.states.size()==1 && g.states[0].bits==Value{s[3]});
}
void whole_ram_output() {
 auto g=base();auto address=input(g,"address",2),enable=input(g,"enable",1);
 g.memories.push_back({"ram",65,4});
 auto low=g.add("memory_read",64,address,constant(0,64),constant(0,64));
 auto high=g.add("memory_read",1,address,constant(0,64),constant(1,64));
 Value read=low;read.insert(read.end(),high.begin(),high.end());
 auto s=g.wire(65,"ram_output","state");
 g.states.push_back({s,g.mux(enable,read,s),{1},0});
 g.ports.push_back({"one_bit",{s[64]},false});
 g.optimize();g.dependencyOrder();
 assert(synchronousRead(g,g.states[0]).has_value());
 assert(pruneUnusedStateBits(g)==0);
 assert(g.states.size()==1 && g.states[0].bits.size()==65);
 assert(synchronousRead(g,g.states[0]).has_value());
}
void duplicate_owner_conservative() {
 auto g=base();auto data=input(g,"data",1),s=g.wire(1,"shared","state");
 g.states.push_back({s,data,{1},0});g.states.push_back({s,data,{1},0});
 g.ports.push_back({"out",s,false});
 // Final pruning itself must avoid dropping either owner. Clock validation
 // independently rejects multiple owners in a real named-edge graph.
 assert(pruneUnusedStateBits(g)==0 && g.states.size()==2);
}
int main() {
 partial_alias_and_reset();bitwise_and_future_cycle();conservative_carry();
 replacement_pin();whole_ram_output();duplicate_owner_conservative();
 std::cout<<"state bit pruning: aliases, reset/enable, future cycle, bitwise lanes, carry, replacement pins, RAM65 PASS\n";
}
'''

with tempfile.TemporaryDirectory(prefix='cpphdl-state-bit-pruning-') as temporary:
    work = Path(temporary)
    toolchain = TestToolchain(args.cxx, None, work)
    source_path = work / 'check.cpp'
    source_path.write_text(source)
    command = toolchain.command([args.cxx, '-std=c++23', '-O1', '-I'+str(root/'include'),
                                 '-I'+str(root/'synth'), source_path, '-o', work/'check'])
    result = subprocess.run(command, env=toolchain.env, capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    result = subprocess.run(toolchain.command([work/'check']), env=toolchain.env,
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    print(result.stdout, end='')
