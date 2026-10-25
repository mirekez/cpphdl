#!/usr/bin/env python3
"""Compare cached mutable RAM graphs with an independent cycle oracle."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

GEN=r'''#include "cpphdl_graph.h"
int main(int argc,char** argv){using namespace cpphdl::graph;Graph g;
g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clk",100000000});
auto address=g.add("input",4,{}, {}, {},"address");
auto data=g.add("input",64,{}, {}, {},"data");
auto we=g.add("input",1,{}, {}, {},"we");
auto re=g.add("input",1,{}, {}, {},"re");
for(auto p:{Port{"address",address,true},Port{"data",data,true},Port{"we",we,true},Port{"re",re,true}})g.ports.push_back(p);
g.memories.push_back({"ram",64,16});
auto row=g.add("memory_read",64,address,constant(0,64),constant(0,64));
auto q=g.add("state",64,{}, {}, {},"q");
g.states.push_back({q,g.mux(re,row,q),{1},0});
g.memoryWrites.push_back({0,address,data,we,0});
g.memoryAccesses.push_back({0,address,{1},false});
g.ports.push_back({"q",q,false});g.ports.push_back({"row",row,false});
g.emit(std::string(argv[1])+"plain.h",7,1);
g.emit(std::string(argv[1])+"cached.h",7,6,false,false,true);
g.emit(std::string(argv[1])+"delta.h",7,6,false,false,true,false,true);
{
auto bad=g;bad.attributes.push_back({"","native.cache_state_roots","1"});bool rejected=false;
try{bad.emit(std::string(argv[1])+"bad_flags.h",7,6,false,false,true,false,true);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("requires parallel roots")!=std::string::npos;}
if(!rejected)throw std::runtime_error("state-cache prerequisite was not checked");
}
g.attributes.push_back({"","native.parallel_roots","1"});
g.attributes.push_back({"","native.cache_state_roots","1"});
{
auto bad=g;bad.add("host_random",32);bool rejected=false;
try{bad.emit(std::string(argv[1])+"bad_host.h",7,6,false,false,true,false,true);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("pure named-edge")!=std::string::npos;}
if(!rejected)throw std::runtime_error("state-cache host effect was not rejected");
}
{
auto bad=g;bad.clockContract=ClockContract::ExplicitEvents;bad.clocks.clear();
for(auto& state:bad.states)state.clock=-1;
for(auto& access:bad.memoryAccesses)access.clock=-1;
for(auto& write:bad.memoryWrites)write.clock=-1;
bool rejected=false;
try{bad.emit(std::string(argv[1])+"bad_clock.h",7,6,false,false,true,false,true);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("pure named-edge")!=std::string::npos;}
if(!rejected)throw std::runtime_error("state-cache clock contract was not checked");
}
g.emit(std::string(argv[1])+"owned.h",7,6,false,false,true,false,true);
}'''
RUN=r'''#define cpphdl_native plain
#include "plain.h"
#undef cpphdl_native
#define cpphdl_native cached
#include "cached.h"
#undef cpphdl_native
#define cpphdl_native delta
#include "delta.h"
#undef cpphdl_native
#define cpphdl_native owned
#include "owned.h"
#undef cpphdl_native
#include <cstdio>
template<class Model>void check(Model& m){uint64_t ram[16]{},q=0;
for(unsigned i=0;i<16000;++i){unsigned a=(i/11)%16;uint64_t data=(uint64_t(i)*0x7138493a71ULL)^7;
if(i%13==0){ram[a]=data^99;m.memory0[a][0]=ram[a];} // Host mutation at a held address.
m.address[0]=a;m.data={uint32_t(data),uint32_t(data>>32)};m.we[0]=i%3!=0;m.re[0]=i%5!=0;
m.clk=false;m.step();if((uint64_t(m.row[1])<<32|m.row[0])!=ram[a])throw 1;
if(m.re[0])q=ram[a];if(m.we[0])ram[a]=data;
m.clk=true;m.step();
if((uint64_t(m.row[1])<<32|m.row[0])!=ram[a]||(uint64_t(m.q[1])<<32|m.q[0])!=q)throw 2;
m.eval(false);if((uint64_t(m.q[1])<<32|m.q[0])!=q)throw 3;
}}
int main(){plain::Model a;cached::Model b;delta::Model c;owned::Model d;
d.__cpphdl_dispatch_policy=cpphdl::graph_runtime::DispatchPolicy::Parallel;
check(a);check(b);check(c);check(d);
puts("PASS mutable RAM memoization: 64000 clock transactions, held addresses, host mutations, read-before-write, settling and parallel owned-state roots");}'''


def main():
    p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);p.add_argument('--work',type=Path,required=True)
    a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);work=a.work.resolve();include=Path(__file__).resolve().parents[2]/'include'
    toolchain = TestToolchain(a.cxx, None, work)
    def run(command,label):
        r=subprocess.run(toolchain.command(command),env=toolchain.env,capture_output=True,text=True,timeout=240)
        (work/(label+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed: '+str(work/(label+'.log')))
        return r.stdout
    (work/'gen.cpp').write_text(GEN);(work/'run.cpp').write_text(RUN)
    run([a.cxx,'-std=c++23','-O1','-pthread','-I'+str(include),work/'gen.cpp','-o',work/'gen'],'build-gen')
    run([work/'gen',str(work)+'/'],'emit')
    run([a.cxx,'-std=c++23','-O1','-pthread','-I'+str(include),work/'run.cpp','-o',work/'run'],'build-run')
    print(run([work/'run'],'run'),end='')


if __name__=='__main__':main()
