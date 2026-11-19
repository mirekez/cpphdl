#include "MemoryContinuation.h"
#include "Mapping.h"
#include "Evaluate.h"
#include "Verilog.h"
#include <deque>
#include <cstdio>

using namespace cpphdl::graph;
int main() {
    try {
        for(bool fullExchange:{false,true}) for(unsigned depth:{1,2,8,32}) for(unsigned contextBits:{32,65,96,512}) {
            Graph g;g.clockContract=ClockContract::RisingEdgeStep;
            std::map<std::string,Value> in;
            for(auto [name,width]:std::map<std::string,unsigned>{{"reset",1},{"command_valid_in",1},
                {"request_ready_in",1},{"context_in",contextBits},{"memory_valid_in",1},{"memory_data_in",32},
                {"memory_error_in",1},{"response_ready_in",1}}) {
                in[name]=g.wire(width,name,"input");g.ports.push_back({name,in[name],true});
            }
            auto out=cpphdl::synth::buildMemoryContinuation(g,"continuation",depth,in,fullExchange);
            for(const auto& [name,value]:out)g.ports.push_back({name,value,false});
            if((depth==1 || (depth==2 && contextBits<=256))?!g.memories.empty():
                (g.memories.size()!=1 || g.memories[0].depth!=depth || g.memories[0].width!=contextBits))
                throw std::runtime_error("continuation lost context RAM");
            (void)cpphdl::synth::verilogText(g,"MemoryContinuation");
            Evaluate sim(cpphdl::synth::mapGates(g));
            struct Reply {unsigned due,id;};std::deque<Reply> replies;
            unsigned sent=0,received=0,maximumPending=0,run=0,maximumRun=0;
            auto context=[](unsigned id){return id*7919u+17u;};
            auto reset=[&] {
                sim.input("reset",1);sim.input("command_valid_in",0);sim.input("request_ready_in",1);
                sim.input("memory_valid_in",0);sim.input("response_ready_in",1);sim.tick();
                replies.clear();sim.input("reset",0);
            };
            reset();
            // Fill without responses and reset both protocol endpoints.
            for(unsigned i=0;i<depth;++i) {
                sim.input("command_valid_in",1);sim.input("context_in",context(900+i));sim.tick();
            }
            reset();
            for(unsigned clock=0;clock<10000 && received<128;++clock) {
                bool requestReady=clock<64 || clock%7!=0;
                bool responseReady=clock<64 || clock%19>=7;
                bool memoryValid=!replies.empty()&&clock>=replies.front().due;
                sim.input("command_valid_in",sent<128);sim.input("request_ready_in",requestReady);
                sim.input("context_in",context(sent));sim.input("response_ready_in",responseReady);
                sim.input("memory_valid_in",memoryValid);
                sim.input("memory_data_in",memoryValid?replies.front().id*3:0);
                sim.input("memory_error_in",memoryValid&&replies.front().id%13==0);
                sim.eval();
                bool push=sim.output("request_valid_out")&&requestReady;
                bool pop=sim.output("response_valid_out")&&responseReady;
                if(bool(sim.output("command_ready_out"))!=(requestReady&&sim.output("request_valid_out")) && sent<128)
                    throw std::runtime_error("request handshake mismatch");
                if(sim.output("response_valid_out")) {
                    if(replies.empty() || sim.output("context_out")!=context(replies.front().id) ||
                        sim.output("data_out")!=replies.front().id*3 ||
                        sim.output("error_out")!=(replies.front().id%13==0))
                        throw std::runtime_error("memory continuation context/data/error mismatch");
                }
                sim.tick();
                if(pop){replies.pop_front();++received;}
                if(push){replies.push_back({clock+40,sent++});}
                if(replies.size()>maximumPending)maximumPending=replies.size();
                run=push?run+1:0;if(run>maximumRun)maximumRun=run;
            }
            if(sent!=128 || received!=128 || !replies.empty() || maximumPending!=depth || maximumRun<depth)
                throw std::runtime_error("memory continuation did not pipeline its request window");
            sim.input("command_valid_in",0);sim.input("memory_valid_in",1);sim.eval();
            if(sim.output("memory_ready_out") || sim.output("response_valid_out"))
                throw std::runtime_error("empty continuation accepted an unowned response");
        }
        std::puts("PASS memory continuations: pipelined requests, ordered contexts, errors, stalls, reset");
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
