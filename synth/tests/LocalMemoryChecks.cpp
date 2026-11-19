#include "LocalMemory.h"
#include "Mapping.h"
#include "Evaluate.h"
#include "Verilog.h"
#include <array>
#include <deque>
#include <cstdio>

using namespace cpphdl::graph;
int main() {
    try {
        Graph g;g.clockContract=ClockContract::RisingEdgeStep;
        std::map<std::string,Value> pins;
        pins["reset"]=g.wire(1,"reset","input");g.ports.push_back({"reset",pins.at("reset"),true});
        for(auto [field,width]:std::map<std::string,unsigned>{{"valid_in",1},{"write_in",1},{"addr_in",32},
            {"size_in",8},{"data_in",64},{"ready_in",1},{"ready_out",1},{"valid_out",1},{"data_out",64},{"error_out",1}}) {
            const bool input=field.find("_in")!=std::string::npos;
            pins["local_out."+field]=g.wire(width,field,input?"input":"wire");
            g.ports.push_back({field,pins.at("local_out."+field),input});
        }
        cpphdl::synth::connectLocalMemory(g,"local",512,64,pins);
        if(g.memories.size()!=8)throw std::runtime_error("local storage lost byte banks or used RAM for two-entry boundaries");
        (void)cpphdl::synth::verilogText(g,"LocalMemory");
        Evaluate sim(cpphdl::synth::mapGates(g));
        std::array<uint8_t,512> memory{};
        struct Reply {uint64_t data,mask;};std::deque<Reply> replies;
        auto reset=[&] {
            sim.input("reset",1);sim.input("valid_in",0);sim.input("ready_in",0);sim.tick();
            sim.input("reset",0);sim.eval();replies.clear();
            if(sim.output("valid_out"))throw std::runtime_error("reset left an old local reply");
        };
        reset();
        unsigned longest=0;
        for(unsigned campaign=0;campaign<3;++campaign) {
            unsigned sent=0,received=0,run=0;
            bool held=false;uint64_t heldData=0;
            for(unsigned clock=0;clock<10000 && received<768;++clock) {
                // Clear before reading; later accesses exercise all byte offsets,
                // straddled words, and writes that preserve neighbouring bytes.
                const bool initialize=sent<62;
                const unsigned size=initialize?8:1+sent%8;
                const unsigned address=initialize?16+8*sent:16+(sent*97u)%(497-size);
                const bool write=initialize || sent%3==0;
                const uint64_t data=uint64_t(sent*0x9e3779b9u)<<32 | (sent*7919u+17u);
                const bool ready=campaign!=0 && (clock<160 || (clock%113>=65 && clock%7!=0));
                sim.input("valid_in",sent<768);sim.input("write_in",write);sim.input("addr_in",address);
                sim.input("size_in",size);sim.input("data_in",data);sim.input("ready_in",ready);sim.eval();
                const auto upstreamReady=sim.output("ready_out");
                sim.input("ready_in",!ready);sim.eval();
                if(sim.output("ready_out")!=upstreamReady)
                    throw std::runtime_error("local request-ready depends combinationally on downstream readiness");
                sim.input("ready_in",ready);sim.eval();
                const bool push=sent<768&&sim.output("ready_out"),pop=ready&&sim.output("valid_out");
                if(held && (!sim.output("valid_out") || sim.output("data_out")!=heldData))
                    throw std::runtime_error("local reply changed under backpressure");
                if(sim.output("valid_out")) {
                    if(replies.empty() || sim.output("error_out") ||
                        (sim.output("data_out")&replies.front().mask)!=replies.front().data)
                        throw std::runtime_error("local reply data or ordering mismatch");
                }
                held=sim.output("valid_out")&&!ready;heldData=sim.output("data_out");
                if(pop){replies.pop_front();++received;}
                if(push) {
                    uint64_t value=0;
                    for(unsigned b=0;b<size;++b) {
                        if(write)memory.at(address+b)=uint8_t(data>>(8*b));
                        else value|=uint64_t(memory.at(address+b))<<(8*b);
                    }
                    replies.push_back({value,write||size==8?~uint64_t(0):(uint64_t(1)<<(8*size))-1});
                    ++sent;
                }
                run=push?run+1:0;longest=std::max(longest,run);
                sim.tick();
                if(campaign==0 && clock==12)break;
            }
            if(campaign && (sent!=768 || received!=768 || !replies.empty()))
                throw std::runtime_error("local replies did not drain");
            reset();
        }
        if(longest<100)throw std::runtime_error("local RAM failed to sustain one access per clock");
        std::printf("PASS local RAM: byte aliases, word crossings, stalls, reset, %u consecutive accesses\n",longest);
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
