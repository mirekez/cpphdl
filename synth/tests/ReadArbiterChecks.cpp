#include "ReadArbiter.h"
#include "Mapping.h"
#include "Evaluate.h"
#include <deque>
#include <cstdio>

using namespace cpphdl::graph;
int main() {
    try {
        for(unsigned sources:{3,8}) for(unsigned depth:{2,8}) for(unsigned replyDepth:{1,2}) {
            Graph g;g.clockContract=ClockContract::RisingEdgeStep;
            auto input=[&](std::string name,unsigned width) {
                auto v=g.wire(width,name,"input");g.ports.push_back({name,v,true});return v;
            };
            std::map<std::string,Value> pins;
            pins["reset"]=input("reset",1);
            for(auto [name,width]:std::map<std::string,unsigned>{{"ready_out",1},{"valid_out",1},{"data_out",64},{"error_out",1}})
                pins["memory_out."+name]=input("bus_"+name,width);
            std::vector<std::map<std::string,Value>> stages(sources);
            for(unsigned i=0;i<sources;++i) {
                auto prefix="s"+std::to_string(i)+"_";
                for(auto [name,width]:std::map<std::string,unsigned>{{"request_valid_out",1},{"addr",32},{"size",8},
                    {"write",1},{"data",64},{"memory_ready_out",1}})
                    stages[i][name]=input(prefix+name,width);
                for(auto [name,width]:std::map<std::string,unsigned>{{"request_ready_in",1},{"memory_valid_in",1},
                    {"memory_data_in",64},{"memory_error_in",1}}) {
                    auto v=g.wire(width,prefix+name);stages[i][name]=v;g.ports.push_back({prefix+name,v,false});
                }
            }
            cpphdl::synth::connectReadStages(g,"bus",depth,64,stages,pins,replyDepth);
            for(auto name:{"valid_in","ready_in","addr_in","size_in","write_in","data_in"})
                g.ports.push_back({"bus_"+std::string(name),pins.at("memory_out."+std::string(name)),false});
            Evaluate sim(cpphdl::synth::mapGates(g));
            auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
            auto payload=[](unsigned id){return 0xabc0000000000000ull+id*7919ull;};
            // Reset while a request is buffered, then start fresh. Subsequent
            // epochs exercise request/reply stalls and the full owner window.
            for(unsigned epoch=0;epoch<2;++epoch) {
                sim.input("reset",1);sim.input("bus_ready_out",0);sim.input("bus_valid_out",0);sim.tick();sim.input("reset",0);
                std::vector<unsigned> sent(sources),received(sources);
                std::vector<std::deque<unsigned>> waiting(sources),issued(sources);
                struct Reply {unsigned due,id;};std::deque<Reply> memory;
                unsigned completed=0;
                for(unsigned cycle=0;cycle<(epoch?10000u:20u) && completed<sources*32;++cycle) {
                    bool busReady=epoch && cycle%17<13;
                    bool memoryValid=!memory.empty() && memory.front().due<=cycle;
                    sim.input("bus_ready_out",busReady);sim.input("bus_valid_out",memoryValid);
                    sim.input("bus_data_out",memoryValid?payload(memory.front().id):0);
                    sim.input("bus_error_out",memoryValid && memory.front().id%7==0);
                    for(unsigned i=0;i<sources;++i) {
                        auto prefix="s"+std::to_string(i)+"_";unsigned id=i*1024+sent[i];
                        sim.input(prefix+"request_valid_out",sent[i]<32);
                        sim.input(prefix+"addr",id*8);sim.input(prefix+"size",1+(id%8));
                        sim.input(prefix+"write",id%2);sim.input(prefix+"data",payload(id));
                        sim.input(prefix+"memory_ready_out",(cycle+i*3)%23<17);
                    }
                    sim.eval();
                    for(unsigned i=0;i<sources;++i) {
                        auto prefix="s"+std::to_string(i)+"_";
                        if(sent[i]<32 && sim.output(prefix+"request_ready_in"))waiting[i].push_back(i*1024+sent[i]++);
                        if(sim.output(prefix+"memory_valid_in")) {
                            require(!issued[i].empty(),"arbiter emitted an unowned response");
                            unsigned id=issued[i].front();
                            require(sim.output(prefix+"memory_data_in")==payload(id) &&
                                sim.output(prefix+"memory_error_in")==unsigned(id%7==0),"arbiter response mismatch");
                            if((cycle+i*3)%23<17){issued[i].pop_front();++received[i];++completed;}
                        }
                    }
                    bool pop=memoryValid && sim.output("bus_ready_in");
                    bool push=busReady && sim.output("bus_valid_in");
                    unsigned id=sim.output("bus_addr_in")/8;
                    if(push) {
                        unsigned source=id/1024;
                        require(source<sources && !waiting[source].empty() && waiting[source].front()==id,"arbiter request owner/order mismatch");
                        require(sim.output("bus_size_in")==1+id%8 && sim.output("bus_write_in")==id%2 &&
                            sim.output("bus_data_in")==payload(id),"arbiter request payload mismatch");
                        waiting[source].pop_front();issued[source].push_back(id);
                    }
                    sim.tick();
                    if(pop)memory.pop_front();
                    if(push)memory.push_back({cycle+11,id});
                }
                if(epoch)require(completed==sources*32 && memory.empty(),"arbiter stalled or dropped requests");
            }
        }
        std::puts("PASS read arbiter: buffered payload/owner, stalls, ordered replies and reset cancellation");
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
