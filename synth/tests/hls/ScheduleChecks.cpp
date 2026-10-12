#include "ScheduledGraph.h"
#include "Mapping.h"
#include "../Evaluate.h"
#include <iostream>
using namespace cpphdl::graph;
using namespace cpphdl::synth;
static Graph bankGraph(unsigned capacity, unsigned address, bool dynamic) {
    ScheduledDesign d;
    d.memoryBytes=capacity; d.blocks.resize(2); d.commandEntry=1;
    std::string banks;
    const auto location=dynamic?"index_in":"("+std::to_string(address)+" + 8 - 8)";
    for(unsigned lane=0;lane<8;++lane) {
        const auto name="storage_lane"+std::to_string(lane);
        if(lane) banks+=", ";
        banks+=name;
        d.blocks[0].statements.push_back(name+" = '0;");
        // Whole-buffer initialization must not build a selector per byte/row.
        d.blocks[0].statements.push_back(name+" = hls_bank_write_"+std::to_string((capacity-16)*8)+"_"+
            std::to_string(lane)+"("+name+", 16, '0, fault);");
        d.blocks[1].statements.push_back("if (operation_in == 1 || operation_in == 2) "+name+" = hls_bank_write_96_"+
            std::to_string(lane)+"("+name+", "+location+", 96'(value_in), fault);");
        d.blocks[1].statements.push_back("if (operation_in == 3) "+name+" = hls_bank_write_port_"+
            std::to_string(lane)+"("+name+", "+location+
            ", 64'(value_in), fault, value_in & 7);");
    }
    d.blocks[0].statements.push_back("booting = 0; phase = 0;");
    d.blocks[1].statements.insert(d.blocks[1].statements.begin(),"if (operation_in == 2) fault = 3;");
    d.blocks[1].statements.push_back("hls_call_result = hls_storage_read_64("+banks+", "+
        location+", fault); result = 64'(hls_call_result); fault = 32'(hls_call_result >> 64); pending = 1; phase = 0;");
    Graph g;
    auto ports=exportScheduledGraph(g,d,"banks");
    for(auto& [name,v]:ports) {
        const bool input=name=="reset" || name.find("_in")==name.size()-3;
        if(input) { auto wire=g.wire(v.size(),name,"input");g.connect(v,wire);v=wire; }
        g.ports.push_back({name,v,input});
    }
    Value retained;
    for(unsigned byte=0;byte<4;++byte) {
        const auto addr=address+byte;
        const auto prefix="banks.storage_lane"+std::to_string(addr%8)+":";
        for(const auto& state:g.states) if(g.nodes[Graph::owner(state.bits.front())].name.find(prefix)==0) {
            auto v=slice(state.bits,(addr/8)*8,8);
            retained.insert(retained.end(),v.begin(),v.end());
        }
    }
    g.ports.push_back({"retained_out",retained,false});
    return g;
}
static void bankChecks() {
    for(unsigned address : {16u,17u,18u,19u,20u,21u,22u,23u,116u}) for(bool dynamic : {false,true}) {
        auto g=bankGraph(128,address,dynamic);
        for(bool gates : {false,true}) {
            Evaluate sim(gates?mapGates(g):g);
            sim.input("reset",1);sim.tick();sim.input("reset",0);sim.tick();
            auto command=[&](unsigned op, unsigned value=0xa1b2c3d4) {
                sim.input("index_in",address);sim.input("operation_in",op);sim.input("value_in",value);
                sim.input("command_valid_in",1);sim.tick();sim.input("command_valid_in",0);
                if(!sim.output("response_valid_out")) throw std::runtime_error("bank response missing");
                const auto result=sim.output("result_out");
                sim.input("response_ready_in",1);sim.tick();sim.input("response_ready_in",0);
                return result;
            };
            if(command(0)!=0 || command(1)!=0xa1b2c3d4 || command(0)!=0xa1b2c3d4)
                throw std::runtime_error("constant/dynamic bank access mismatch");
            if(command(3,0x10203042)!=0xa1b23042 || command(3,0x12345678)!=0xa1b23042 || command(0)!=0xa1b23042)
                throw std::runtime_error("partial/zero-size bank write changed other bytes");
            command(2,0x10203040);
            if(sim.output("fault_out")!=3) throw std::runtime_error("faulted bank write was accepted");
            if(sim.output("retained_out")!=0xa1b23042)
                throw std::runtime_error("faulted bank write changed retained bytes");
        }
    }
    for(unsigned address : {0u,15u,121u,128u,65535u}) for(bool dynamic : {false,true}) {
        Evaluate sim(bankGraph(128,address,dynamic));
        sim.input("reset",1);sim.tick();sim.input("reset",0);sim.tick();
        sim.input("index_in",address);sim.input("command_valid_in",1);sim.tick();
        if(sim.output("fault_out")!=3 || !sim.output("response_valid_out") || sim.output("result_out"))
            throw std::runtime_error("invalid constant/dynamic bank read did not fault");
    }
    const auto small=bankGraph(128,17,false), large=bankGraph(1056,17,false);
    std::cout<<"bank graph scaling: "<<small.nodes.size()<<" -> "<<large.nodes.size()<<" nodes\n";
    const auto selectors=[](const Graph& g) {
        return std::count_if(g.nodes.begin(),g.nodes.end(),[](const auto& n) {
            return n.op=="eq" || n.op=="shl" || n.op=="shr";
        });
    };
    // State and byte muxes grow linearly; constant accesses need no new decoders.
    if(large.nodes.size()>small.nodes.size()+4*(1056-128) || selectors(large)!=selectors(small))
        throw std::runtime_error("fixed-address initialization grows into a row-selector network");
}
int main() {
    try {
        bankChecks();
        ScheduledDesign d;
        d.symbols["values.counter"]={32,"counter",true};d.clocked.insert("values.counter");
        d.blocks.resize(4);d.resetEntry=0;d.commandEntry=1;
        d.blocks[0].statements={"values.counter = 0; booting = 0; phase = 0;"};
        d.blocks[1].statements={"values.counter = value_in;"};d.blocks[1].yes=2;d.blocks[1].suspend=true;
        d.blocks[2].statements={"values.counter = 32'(values.counter + 1);"};
        d.blocks[2].condition="values.counter < 3";d.blocks[2].yes=2;d.blocks[2].no=3;d.blocks[2].suspend=true;
        d.blocks[3].statements={"result = values.counter; pending = 1; phase = 0;"};
        Graph graph;graph.clockContract=ClockContract::RisingEdgeStep;
        auto ports=exportScheduledGraph(graph,d,"worker");
        for(auto& [name,v]:ports) {
            bool input=name=="reset" || name.find("_in")==name.size()-3;
            if(input) {auto wire=graph.wire(v.size(),name,"input");graph.connect(v,wire);v=wire;}
            graph.ports.push_back({name,v,input});
        }
        auto check=[&](Graph g) {
            Evaluate sim(g);sim.input("reset",1);sim.tick();sim.input("reset",0);sim.tick();
            if(!sim.output("command_ready_out")) throw std::runtime_error("boot boundary lost");
            sim.input("command_valid_in",1);sim.input("value_in",0);sim.tick();sim.input("command_valid_in",0);
            for(unsigned step=0;step<3;++step) {
                if(sim.output("response_valid_out")) throw std::runtime_error("loop executed without its clock boundary");
                sim.tick();
            }
            if(!sim.output("response_valid_out") || sim.output("result_out")!=3) throw std::runtime_error("loop result lost");
            for(unsigned step=0;step<3;++step) sim.tick();
            if(!sim.output("response_valid_out")) throw std::runtime_error("response not retained");
            sim.input("response_ready_in",1);sim.tick();if(!sim.output("command_ready_out")) throw std::runtime_error("handshake failed");
        };
        check(graph);check(mapGates(graph));
        d.blocks[1].statements={"result = unknown_helper(value_in);"};
        bool rejected=false;try{Graph g;exportScheduledGraph(g,d,"bad");}catch(const std::runtime_error&){rejected=true;}
        if(!rejected) throw std::runtime_error("unsupported schedule accepted");
        std::cout<<"direct schedule graph: boot, loop clock boundaries, handshake and rejection passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
