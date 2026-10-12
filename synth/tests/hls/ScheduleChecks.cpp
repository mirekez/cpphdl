#include "ScheduledGraph.h"
#include "Mapping.h"
#include "../Evaluate.h"
#include <iostream>
using namespace cpphdl::graph;
using namespace cpphdl::synth;
static void commandJoins() {
    for (unsigned test = 0; test < 5; ++test) {
        ScheduledDesign d;
        d.symbols["scratch.local"]={32,"private arithmetic",true};
        d.blocks.resize(7); d.resetEntry=0; d.commandEntry=1;
        d.blocks[1].condition="operation_in & 1";
        d.blocks[1].yes=2; d.blocks[1].no=3;
        d.blocks[2].statements={"scratch.local = value_in + 1;"}; d.blocks[2].yes=4;
        d.blocks[3].statements={"scratch.local = value_in - 1;"}; d.blocks[3].yes=4;
        d.blocks[4].statements={"result = value_in * 7;"};
        if (test == 1) {
            // A join inside one conditional region must retain that region's
            // activation; its sibling returns instead of entering the join.
            d.blocks[3].yes=-1; d.blocks[3].statements={"result = value_in + 5;"};
            d.blocks[2].condition="index_in & 1";d.blocks[2].yes=5;d.blocks[2].no=6;
            d.blocks[5].yes=4;d.blocks[6].yes=4;
        } else if (test == 2) d.blocks[2].statements={"fault = 9;"};
        else if (test == 3) {
            // An omitted false successor is an exit, not an unconditional edge.
            d.blocks[1].no=-1;d.blocks[1].yes=4;
        } else if (test == 4) d.blocks[1].yes=d.blocks[1].no=4;
        Graph graph;auto ports=exportCombinationalCommand(graph,d,"joins");
        for(auto& node:graph.nodes) if(node.op=="wire") node.op="input";
        for(const auto& [name,bits]:ports)
            if(name=="operation_in"||name=="index_in"||name=="value_in"||name=="result_out"||name=="fault_out")
                graph.ports.push_back({name,bits,name.find("_in")!=std::string::npos});
        graph.optimize();
        if (test==0 || test==4) {
            std::set<size_t> visited;
            std::function<void(Value)> walk=[&](Value value) {
                for(auto bit:graph.resolved(value)) if(bit>1 && visited.insert(Graph::owner(bit)).second) {
                    const auto& node=graph.nodes[Graph::owner(bit)];
                    if(node.op=="input" && node.name.find("operation_in")!=std::string::npos)
                        throw std::runtime_error("private helper branch serialized independent command arithmetic");
                    walk(node.left);walk(node.right);walk(node.select);
                }
            };
            walk(ports.at("result_out"));
        }
        for(auto g:{graph,mapGates(graph)}) {
            Evaluate sim(g);
            for(unsigned op=0;op<4;++op) for(unsigned index=0;index<2;++index)
                for(unsigned value=0;value<32;++value) {
                    sim.input("operation_in",op);sim.input("index_in",index);sim.input("value_in",value);sim.eval();
                    unsigned answer=value*7,fault=0;
                    if(test==1 && !(op&1)) answer=value+5;
                    if(test==2 && (op&1)) {answer=0;fault=9;}
                    if(test==3 && !(op&1)) answer=0;
                    if(sim.output("result_out")!=answer || sim.output("fault_out")!=fault)
                        throw std::runtime_error("combinational join changed conditional/fault behavior");
                }
        }
    }
}
int main() {
    try {
        commandJoins();
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
        std::cout<<"direct schedule graph: command joins, conditional exits, faults, boot, loop clock boundaries, handshake and rejection passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
