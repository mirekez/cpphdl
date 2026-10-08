#include "Mapping.h"
#include "Evaluate.h"
#include <iostream>
using namespace cpphdl::graph;
int main() {
    try {
        uint64_t random=0xabc98f721;
        for(unsigned width:{4,8,16,32,64}) {
            Graph graph; graph.clockContract=ClockContract::RisingEdgeStep;
            auto a=graph.wire(width,"a","input"),b=graph.wire(width,"b","input");
            graph.ports={{"a",a,true},{"b",b,true}};
            for(const std::string op:{"add","sub","mul","div","mod","sdiv","smod","lt","slt","eq","shl","shr","sar","and","or","xor"})
                graph.ports.push_back({op,graph.binary(op,a,b,(op=="lt"||op=="slt"||op=="eq")?1:width),false});
            for(const std::string op:{"any","all","parity"}) graph.ports.push_back({op,graph.unary(op,a),false});
            auto mapped=cpphdl::synth::mapGates(graph);
            for(auto n:mapped.dependencyOrder()) {
                const auto& node=mapped.nodes[n];
                if(node.op!="input" && (node.width!=1 || (node.op!="and" && node.op!="or" && node.op!="xor" && node.op!="mux")))
                    throw std::runtime_error("word operation remains after mapping");
            }
            Evaluate reference(graph), gates(mapped);
            for(unsigned sample=0;sample<256;++sample) {
                random^=random<<13;random^=random>>7;random^=random<<17;
                uint64_t av=width==4?sample/16:random,bv=width==4?sample%16:(sample<70?sample:random>>1);
                if(sample==1 && width!=4) av=uint64_t(1)<<(width-1);
                for(auto* sim:{&reference,&gates}) {sim->input("a",av);sim->input("b",bv);sim->eval();}
                for(auto& p:graph.ports) if(!p.input && reference.output(p.name)!=gates.output(p.name))
                    throw std::runtime_error(p.name+" width="+std::to_string(width)+" a="+std::to_string(av)+" b="+std::to_string(bv));
            }
        }
        Graph narrow; narrow.clockContract=ClockContract::RisingEdgeStep;
        auto a=narrow.wire(16,"a","input"), b=narrow.wire(16,"b","input");
        narrow.ports={{"a",a,true},{"b",b,true}};
        for(const std::string op:{"div","mod","sdiv","smod"})
            narrow.ports.push_back({op,narrow.binary(op,a,b,8),false});
        auto narrowGates=cpphdl::synth::mapGates(narrow);
        Evaluate reference(narrow), gates(narrowGates);
        for(uint64_t av:{1000u,65535u,32768u}) for(uint64_t bv:{0u,5u,300u,65531u}) {
            for(auto* sim:{&reference,&gates}) {sim->input("a",av);sim->input("b",bv);sim->eval();}
            for(const auto& p:narrow.ports) if(!p.input && reference.output(p.name)!=gates.output(p.name))
                throw std::runtime_error("narrow result: "+p.name);
        }
        Graph memory; memory.clockContract=ClockContract::NamedEdges;
        memory.clocks={{"main_clk",100},{"memory_clk",50}};
        memory.memories.push_back({"words",8,6});
        auto address=memory.wire(3,"address","input"), shortAddress=memory.wire(2,"short_address","input");
        auto data=memory.wire(8,"data","input"), enabled=memory.wire(1,"enabled","input");
        memory.ports={{"address",address,true},{"short_address",shortAddress,true},{"data",data,true},{"enabled",enabled,true},
                      {"read",memory.add("memory_read",8,address,constant(0,64),constant(0,64)),false},
                      {"short_read",memory.add("memory_read",8,shortAddress,constant(0,64),constant(0,64)),false}};
        memory.memoryWrites.push_back({0,shortAddress,data,enabled,1,true});
        auto product=memory.binary("mul",data,data,8);
        memory.memoryWrites[0].data=product;
        memory.memoryAccesses.push_back({0,shortAddress,enabled,true,1,true});
        auto state=memory.wire(8,"held","state");
        memory.nodes.back().left=constant(23,8);
        memory.states.push_back({state,product,{1},0,false,enabled,constant(17,8)});
        memory.ports.push_back({"held",state,false});
        auto expanded=cpphdl::synth::expandGates(memory,{Graph::owner(product[0])});
        expanded.validateClocks();
        if(expanded.memories.size()!=1 || expanded.memoryWrites.size()!=1 || expanded.memoryAccesses.size()!=1 ||
           expanded.memoryWrites[0].clock!=1 || !expanded.memoryWrites[0].falling ||
           !expanded.memoryAccesses[0].transaction || expanded.memoryAccesses[0].clock!=1 || !expanded.memoryAccesses[0].falling ||
           expanded.states.size()!=1 || expanded.states[0].clock!=0 || expanded.states[0].falling ||
           number(expanded.nodes[Graph::owner(expanded.states[0].bits[0])].left)!=23 ||
           number(expanded.states[0].resetValue)!=17 || expanded.states[0].reset.empty())
            throw std::runtime_error("partial mapping changed memory, initialization or clock/reset metadata");
        for(auto n:expanded.dependencyOrder()) if(expanded.nodes[n].op=="mul")
            throw std::runtime_error("selected arithmetic was not expanded");
        Evaluate expandedRam(cpphdl::synth::mapGates(expanded));
        expandedRam.input("enabled",1); expandedRam.input("short_address",1); expandedRam.input("address",1);
        expandedRam.input("data",93); expandedRam.tick(); expandedRam.input("enabled",0); expandedRam.eval();
        if(expandedRam.output("read")!=((93u*93u)&255))
            throw std::runtime_error("partial mapping lost the arithmetic memory write");
        // Keep the original memory read/write test independent of partial mapping.
        memory.memoryWrites[0].data=data;
        memory.states.clear(); memory.ports.pop_back();
        auto memoryGates=cpphdl::synth::mapGates(memory);
        memoryGates.validateClocks();
        for(const auto& state:memoryGates.states)
            if(state.clock!=1 || !state.falling) throw std::runtime_error("mapped memory lost its clock/edge owner");
        Evaluate ram(memoryGates);
        ram.input("enabled",1);ram.input("short_address",1);ram.input("data",93);ram.tick();ram.input("enabled",0);
        for(unsigned row=0;row<8;++row) {
            ram.input("address",row);ram.eval();
            if(ram.output("read")!=(row==1?93u:0u)) throw std::runtime_error("narrow memory address aliased or invalid row read");
            if(ram.output("short_read")!=93) throw std::runtime_error("narrow memory read selected an unreachable row");
        }
        Graph bad;bad.clockContract=ClockContract::RisingEdgeStep;
        bad.ports.push_back({"result",bad.add("unknown",1,{0}),false});
        bool rejected=false;try{cpphdl::synth::mapGates(bad);}catch(const std::runtime_error&){rejected=true;}
        if(!rejected) throw std::runtime_error("unmapped operation accepted");
        Graph scopes;
        auto lhs=scopes.wire(8,"lhs","input"), rhs=scopes.wire(8,"rhs","input");
        scopes.currentScope="first";
        auto first=scopes.binary("add",lhs,rhs,8);
        scopes.currentScope="second";
        auto second=scopes.binary("add",lhs,rhs,8);
        scopes.ports={{"lhs",lhs,true},{"rhs",rhs,true},{"first",first,false},{"second",second,false}};
        auto scoped=cpphdl::synth::expandGates(scopes,{Graph::owner(first[0]),Graph::owner(second[0])});
        for(const auto& port:scoped.ports) if(!port.input) for(auto bit:scoped.resolved(port.bits))
            if(bit>1 && scoped.nodes[Graph::owner(bit)].scope!=port.name)
                throw std::runtime_error("mapping shared arithmetic across retiming scopes");
        std::cout<<"native gate mapping: arithmetic, reductions, signed division, shifts and rejection passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
