// Compile with GENERAL_EMIT to construct graphs, otherwise benchmark their
// emitted native models. No RTL clocks are batched or skipped.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

constexpr uint64_t SEED = 0x9e3779b97f4a7c15ull;
enum Kind { Alu, Shared, Bram, LargeRam, Control, Pipeline, Dependent, Bus, Mixed };
constexpr unsigned kind = GENERAL_KIND;
constexpr unsigned depth = kind == Bram ? 131072 : kind == LargeRam ? 8388608 : 0;
constexpr unsigned branches = kind == Dependent ? 1 : kind == Pipeline ? 64 : 8;
constexpr unsigned rounds = kind == Alu || kind == Mixed ? 192 : kind == Bram ? 32 : kind == LargeRam ? 16 : 0;
inline uint64_t mix(uint64_t value, unsigned round, unsigned branch) {
    value += SEED + round * 131 + branch;
    value ^= value >> 13;
    return value * 0x100000001b3ull;
}

#ifdef GENERAL_EMIT
#include <cpphdl_graph_native.h>
#include <fstream>
using namespace cpphdl::graph;
Graph makeGraph(unsigned which = kind) {
    Graph g; g.clockContract = ClockContract::RisingEdgeStep;
    auto input = [&](const char* name, unsigned width) {
        auto v = g.wire(width, name, "input"); g.ports.push_back({name,v,true}); return v;
    };
    auto data=input("data",64),enable=input("enable",1),reset=input("reset",1);
    std::vector<Value> states;
    unsigned count=which==Control?8:branches;
    for(unsigned b=0;b<count;++b)states.push_back(g.wire(64,"state"+std::to_string(b),"state"));
    auto math=[&](Value v,unsigned r,unsigned b) {
        v=g.binary("add",v,constant(SEED+r*131+b,64),64);
        v=g.binary("xor",v,g.binary("shr",v,constant(13,64),64),64);
        return g.binary("mul",v,constant(0x100000001b3ull,64),64);
    };
    Value common=data;
    if(which==Shared || which==Dependent)
        for(unsigned r=0;r<(which==Dependent?2048:768);++r)common=math(common,r,11);
    if(depth)g.memories.push_back({"ram",64,depth});
    for(unsigned b=0;b<count;++b) {
        auto v=g.binary("xor",states[b],data,64);
        if(which==Alu || which==Mixed || which==Bram || which==LargeRam) {
            for(unsigned r=0;r<rounds;++r) {
                if(depth) {
                    auto addr=resize(g.binary("add",v,constant(r+b,64),64),kind==Bram?17:23);
                    v=g.binary("xor",v,g.add("memory_read",64,addr,constant(0,64),constant(0,64)),64);
                }
                v=math(v,r,b);
            }
        } else if(which==Shared || which==Dependent) {
            v=g.binary("xor",states[b],common,64);
            for(unsigned r=0;r<16;++r)v=math(v,r,b);
        } else if(which==Pipeline) {
            v=g.binary("xor",b?states[b-1]:data,constant(SEED+b,64),64);
            for(unsigned r=0;r<4;++r)v=math(v,r,b);
        } else if(which==Control) {
            auto select=g.binary("eq",slice(states[b],0,3),slice(data,b,3),1);
            auto increment=g.binary("add",states[b],constant(b+1,64),64);
            auto rotate=g.binary("xor",g.binary("shr",states[b],constant(1,64),64),data,64);
            v=g.mux(select,rotate,increment);
        } else if(which==Bus) {
            for(unsigned r=0;r<4;++r)v=math(v,r,b);
        }
        v=g.mux(reset,constant(0,64),g.mux(enable,v,states[b]));
        g.states.push_back({states[b],v,{1}});
    }
    for(unsigned b=0;b<8;++b)g.ports.push_back({"out"+std::to_string(b),states[(b*(which==Pipeline?8:1))%count],false});
    if(depth) {
        auto writeEnable=g.binary("and",enable,g.unary("not",reset),1);
        auto addr=resize(data,kind==Bram?17:23);
        g.memoryWrites.push_back({0,addr,data,writeEnable});
        // Same-address priority must be preserved after parallel evaluation.
        auto secondEnable=g.binary("and",writeEnable,slice(data,3,1),1);
        g.memoryWrites.push_back({0,addr,g.binary("xor",data,constant(SEED,64),64),secondEnable});
    }
    return g;
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    std::ofstream manifest(std::string(argv[1])+"/plans.tsv");
    for(unsigned lanes:{1u,2u,4u,6u}) {
        auto g=makeGraph();auto nodes=g.nodes.size();
        auto plan=nativeParallelPlan(g,g.dependencyOrder(),lanes);
        manifest<<lanes<<'\t'<<plan.lanes<<'\t'<<nodes<<'\t'<<g.nodes.size()<<'\n';
        auto emitted=makeGraph();
        auto path=std::string(argv[1])+"/model"+std::to_string(lanes)+".h";
        emitted.emit(path,512,lanes);
        // Some requested counts compact to identical headers. Include each
        // under a distinct namespace even when GCC deduplicates identical
        // #pragma-once files by contents.
        std::ifstream input(path);std::string text((std::istreambuf_iterator<char>(input)),{});input.close();
        auto start=text.find("#pragma once");if(start!=std::string::npos)text.replace(start,12,"// model "+std::to_string(lanes));
        std::ofstream(path)<<text;
    }
    if(kind==Mixed){auto g=makeGraph(Control);g.emit(std::string(argv[1])+"/small.h",512,1);}
}
#else
#define cpphdl_native serial
#include "model1.h"
#undef cpphdl_native
#define cpphdl_native inner2
#include "model2.h"
#undef cpphdl_native
#define cpphdl_native inner4
#include "model4.h"
#undef cpphdl_native
#define cpphdl_native inner6
#include "model6.h"
#undef cpphdl_native
#if GENERAL_KIND == 8
#define cpphdl_native small
#include "small.h"
#undef cpphdl_native
#endif
#include <cpphdl_graph_batch.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <stdexcept>

template<class Model> auto outputs(const Model& m) {
    std::array<uint64_t,8> out{};
    auto ports=std::array{m.out0,m.out1,m.out2,m.out3,m.out4,m.out5,m.out6,m.out7};
    for(unsigned b=0;b<8;++b)out[b]=ports[b][0]|(uint64_t(ports[b][1])<<32);
    return out;
}
template<class Model> struct Actor {
    Model model;
#if GENERAL_KIND == 8
    small::Model cheap;
#endif
    unsigned id=0,total=0;
    void initialize(unsigned index,unsigned count){
        id=index;total=count;
        if constexpr(depth)for(unsigned a=0;a<depth;++a)model.memory0[a][0]=mix(SEED*(a+1),id,3);
    }
    void tick(uint64_t data,bool enable,bool reset) {
        auto step=[&](auto& m){m.data={uint32_t(data),uint32_t(data>>32)};m.enable[0]=enable;m.reset[0]=reset;m.step();};
#if GENERAL_KIND == 8
        if(id>=std::max(1u,total/4))step(cheap);else step(model);
#else
        step(model);
#endif
    }
    auto out()const{
#if GENERAL_KIND == 8
        if(id>=std::max(1u,total/4))return outputs(cheap);
#endif
        return outputs(model);
    }
    uint64_t memoryHash()const{
        uint64_t hash=0;
        if constexpr(depth)for(unsigned a=0;a<depth;++a)hash=hash*131+model.memory0[a][0];
        return hash;
    }
};
struct Oracle {
    std::array<uint64_t,branches> state{};
    std::vector<uint64_t> ram;
    unsigned id=0,total=0;
    void initialize(unsigned index,unsigned count) {
        id=index;total=count;ram.resize(depth);
        for(unsigned a=0;a<depth;++a)ram[a]=mix(SEED*(a+1),id,3);
    }
    void tick(uint64_t data,bool enable,bool reset) {
        if(reset){state.fill(0);return;}if(!enable)return;
        auto old=state;uint64_t common=data;
        if(kind==Shared||kind==Dependent)for(unsigned r=0;r<(kind==Dependent?2048:768);++r)common=mix(common,r,11);
        bool cheap=kind==Control||(kind==Mixed&&id>=std::max(1u,total/4));
        for(unsigned b=0;b<branches;++b) {
            auto v=old[b]^data;
            if(cheap) {
                v=(old[b]&7)==((data>>b)&7)?((old[b]>>1)^data):old[b]+b+1;
            } else if(kind==Alu||kind==Mixed||depth) {
                for(unsigned r=0;r<rounds;++r){if(depth)v^=ram[(v+r+b)&(depth-1)];v=mix(v,r,b);}
            } else if(kind==Shared||kind==Dependent) {
                v=old[b]^common;for(unsigned r=0;r<16;++r)v=mix(v,r,b);
            } else if(kind==Pipeline) {
                v=(b?old[b-1]:data)^(SEED+b);for(unsigned r=0;r<4;++r)v=mix(v,r,b);
            } else if(kind==Bus)for(unsigned r=0;r<4;++r)v=mix(v,r,b);
            state[b]=v;
        }
        if(depth)ram[data&(depth-1)]=(data&8)?data^SEED:data;
    }
    auto out()const{std::array<uint64_t,8> out{};for(unsigned b=0;b<8;++b)out[b]=state[(b*(kind==Pipeline?8:1))%branches];return out;}
    uint64_t memoryHash()const{uint64_t hash=0;for(auto value:ram)hash=hash*131+value;return hash;}
};
struct Drive {uint64_t data=0;bool enable=false,reset=false;};
// A deterministic shared controller, separate from the actors. One response
// per clock, variable 7..11-clock latency, outstanding requests and stalls.
// It models bus scheduling, not FPGA DRAM electrical/timing implementation.
struct Controller {
    std::vector<uint64_t> memory;
    std::vector<uint64_t> due,addresses;
    uint64_t traffic=0;
    explicit Controller(unsigned n):due(n,0),addresses(n,0) {
        if(kind==Bus){memory.resize(1<<20);for(unsigned a=0;a<memory.size();++a)memory[a]=mix(a+1,7,9);}
    }
    template<class Actors>void prepare(uint64_t cycle,Actors& actors,std::vector<Drive>& drive) {
        for(unsigned i=0;i<drive.size();++i)drive[i]={SEED*(cycle+1)+i*7919,(cycle+i)%7!=0,cycle%1021==0};
        if(kind!=Bus)return;
        if(cycle%1021==0){std::fill(due.begin(),due.end(),0);return;}
        for(unsigned i=0;i<drive.size();++i){drive[i].enable=false;if(!due[i]){addresses[i]=(actors[i].out()[0]^SEED*i)&(memory.size()-1);due[i]=cycle+7+(cycle+i)%5;}}
        if(cycle%13==4)return; // Backpressure on the shared response channel.
        for(unsigned offset=0;offset<drive.size();++offset){unsigned i=(cycle+offset)%drive.size();
            if(due[i]&&due[i]<=cycle){auto address=addresses[i];drive[i].enable=true;drive[i].data=memory[address];
                memory[address]=mix(memory[address],unsigned(cycle),i);traffic=traffic*131+address+drive[i].data;due[i]=0;break;}}
    }
};
struct Result {uint64_t fingerprint=0,memory=0,traffic=0;double seconds=0,cpu=0;};
template<class ActorType>Result execute(unsigned cycles,unsigned instances,unsigned workers,const std::string& mode,bool verify) {
    std::vector<ActorType> actors(instances);for(unsigned i=0;i<instances;++i)actors[i].initialize(i,instances);
    // One executor for the entire benchmark, including inner-lane modes.
    // Separate model storage is retained; no inactive per-instance pools remain.
    if constexpr(requires(ActorType& actor){actor.model.__cpphdl_threads;}) {
        auto shared=std::make_shared<cpphdl::graph_runtime::Threads>(workers,65536);
        for(auto& actor:actors)actor.model.__cpphdl_threads=shared;
    }
    std::unique_ptr<cpphdl::graph_runtime::Batch> batch;
    if(mode.starts_with("batch")||mode.starts_with("dynamic"))batch=std::make_unique<cpphdl::graph_runtime::Batch>(workers);
    Controller controller(instances);std::vector<Drive> drive(instances);Result result;
    auto schedule=mode.starts_with("dynamic")?cpphdl::graph_runtime::Batch::Schedule::Dynamic:cpphdl::graph_runtime::Batch::Schedule::Static;
    const auto started=std::chrono::steady_clock::now();const auto cpu=std::clock();
    for(unsigned cycle=0;cycle<cycles;++cycle) {
        controller.prepare(cycle,actors,drive);
        auto step=[&](std::size_t i){auto& d=drive[i];actors[i].tick(d.data,d.enable,d.reset);};
        if(batch)batch->run(instances,step,schedule);else for(unsigned i=0;i<instances;++i)step(i);
        // Only the caller consumes ports, after every sibling has published.
        for(unsigned i=0;i<instances;++i)for(auto value:actors[i].out())result.fingerprint=result.fingerprint*131+value;
    }
    result.cpu=double(std::clock()-cpu)/CLOCKS_PER_SEC;
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    // Full mutable-memory comparison is outside the timed interval.
    if(verify)for(auto& actor:actors)result.memory=result.memory*131+actor.memoryHash();
    result.traffic=controller.traffic;return result;
}
int main(int argc,char** argv) {
    if(argc!=5)return 1;
    std::string mode=argv[1];unsigned cycles=std::stoul(argv[2]),instances=std::stoul(argv[3]);bool verify=std::stoul(argv[4]);
    if(!cycles||!instances||instances>256)throw std::invalid_argument("invalid benchmark dimensions");
    unsigned workers=mode=="direct"?1:unsigned(mode.back()-'0');
    if(workers!=1&&workers!=2&&workers!=4&&workers!=6)throw std::invalid_argument("invalid benchmark workers");
    Result result;
    if(mode=="inner2")result=execute<Actor<inner2::Model>>(cycles,instances,workers,mode,verify);
    else if(mode=="inner4")result=execute<Actor<inner4::Model>>(cycles,instances,workers,mode,verify);
    else if(mode=="inner6")result=execute<Actor<inner6::Model>>(cycles,instances,workers,mode,verify);
    else result=execute<Actor<serial::Model>>(cycles,instances,workers,mode,verify);
    if(verify){auto oracle=execute<Oracle>(cycles,instances,1,"direct",true);
        if(result.fingerprint!=oracle.fingerprint||result.memory!=oracle.memory||result.traffic!=oracle.traffic)
            throw std::runtime_error("independent scalar oracle mismatch");}
    std::printf("{\"mode\":\"%s\",\"cycles\":%u,\"instances\":%u,\"workers\":%u,\"seconds\":%.9f,\"cpu_seconds\":%.9f,\"effective_cpus\":%.4f,\"fingerprint\":\"%016llx\",\"memory_hash\":\"%016llx\",\"traffic_hash\":\"%016llx\",\"verified\":%s}\n",
        mode.c_str(),cycles,instances,workers,result.seconds,result.cpu,result.cpu/result.seconds,
        (unsigned long long)result.fingerprint,(unsigned long long)result.memory,(unsigned long long)result.traffic,verify?"true":"false");
}
#endif
