// Verify that direct lowering retains word arithmetic at every FpAdd register.
#define CPPHDL_GRAPH_CORE_ONLY
#include <cpphdl_graph.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
using namespace cpphdl::graph;
int main(int argc,char**argv){
    if(argc!=3)return 2;
    std::ifstream file(argv[1]);std::ostringstream text;text<<file.rdbuf();Graph graph;graph.load(text.str().c_str());
    if(graph.clockContract!=ClockContract::RisingEdgeStep)throw std::runtime_error("not a direct C++RTL clock contract");
    unsigned sums=0,multiplies=0;
    for(const auto& node:graph.nodes)if(node.op=="mul" && node.width>=48)++multiplies;
    for(const auto& state:graph.states){
        if(state.bits.empty() || graph.nodes[Graph::owner(state.bits[0])].name.find(".adder.sum_reg:")==std::string::npos)continue;
        ++sums;std::set<size_t> seen;bool add=false;
        auto visit=[&](auto&& self,Bit bit)->void{
            bit=graph.resolve(bit);if(bit<2)return;auto index=Graph::owner(bit);if(!seen.insert(index).second)return;
            const auto& node=graph.nodes[index];
            if(node.op=="add" && node.width==32){add=true;return;}
            // The sum register's data path must reach a word addition through
            // only its enable/reset muxes, not a ripple-carry gate network.
            if(node.op!="mux")return;
            for(auto b:node.left)self(self,b);for(auto b:node.right)self(self,b);
        };
        for(auto b:state.next)visit(visit,b);
        if(!add)throw std::runtime_error("FpAdd sum register lost its 32-bit add");
    }
    if(sums!=std::stoul(argv[2]) || multiplies<sums)throw std::runtime_error("missing MAC arithmetic");
    std::cout<<"{\"status\":\"passed\",\"clock_contract\":\"RisingEdgeStep\",\"fpadd_word_sums\":"<<sums
             <<",\"wide_multiply_nodes\":"<<multiplies<<",\"nodes\":"<<graph.nodes.size()<<"}\n";
}
