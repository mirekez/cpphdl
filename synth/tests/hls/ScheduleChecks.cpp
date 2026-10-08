#include "ScheduledGraph.h"
#include "Mapping.h"
#include "../Evaluate.h"
#include <iostream>
using namespace cpphdl::graph;
using namespace cpphdl::synth;
int main() {
    try {
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
