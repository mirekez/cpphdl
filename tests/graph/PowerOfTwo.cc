#include "cpphdl_graph.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace cpphdl::graph;
static uint64_t wiring(Graph& graph,const Value& output,const Value& input,Bit enable,uint64_t x,bool active) {
    std::function<bool(Bit)> evaluate=[&](Bit bit)->bool {
        bit=graph.resolve(bit);if(bit<2)return bit;
        auto at=std::find(input.begin(),input.end(),bit);if(at!=input.end())return (x>>unsigned(at-input.begin()))&1;
        if(bit==enable)return active;
        const auto& node=graph.nodes[Graph::owner(bit)];auto lane=Graph::lane(bit);
        if(node.op!="and")throw std::runtime_error("constant arithmetic retained a non-wiring cell");
        return evaluate(node.left[lane])&&evaluate(node.right[lane]);
    };
    uint64_t result=0;for(unsigned lane=0;lane<output.size();++lane)if(evaluate(output[lane]))result|=uint64_t(1)<<lane;
    return result;
}
int main(){
    uint64_t random=0x123456789abcdefull,checks=0;
    for(unsigned width:{1u,5u,8u,16u,31u,32u,63u,64u})for(unsigned output_width:{1u,8u,32u,64u})
    for(unsigned shift:{0u,1u,3u,7u,15u,31u,63u})for(bool late:{false,true})for(bool gated:{false,true})for(unsigned operation=0;operation<4;++operation){
        Graph graph;auto input=graph.wire(width,"x","input");auto enable=graph.wire(1,"enable","input");
        auto coefficient=constant(uint64_t(1)<<shift,64);if(gated)coefficient[shift]=enable[0];auto actual=coefficient;
        Value alias;if(late){alias=graph.wire(64,"parameter");coefficient=alias;}
        const std::string op=operation==0?"div":operation==1?"mod":"mul";
        auto result=operation==3?graph.add(op,output_width,coefficient,input):graph.add(op,output_width,input,coefficient);
        if(late)graph.connect(alias,actual);
        graph.ports.push_back({"x",input,true});graph.ports.push_back({"enable",enable,true});graph.ports.push_back({"y",result,false});
        // Exercise serialized graphs too: synthesis loads graphs produced by
        // a separately built frontend before constants become final aliases.
        std::ostringstream text;graph.save(text);Graph loaded;loaded.load(text.str().c_str());loaded.optimize();
        for(unsigned sample=0;sample<32;++sample){
            random=random*6364136223846793005ull+1442695040888963407ull;
            uint64_t x=(sample==0?0:sample==1?~uint64_t(0):random)&mask(width),factor=gated&&!(sample&1)?0:(uint64_t(1)<<shift);
            uint64_t expected=(operation==0?(factor?x/factor:0):operation==1?(factor?x%factor:0):x*factor)&mask(output_width);
            if(wiring(loaded,result,input,enable[0],x,sample&1)!=expected)throw std::runtime_error("power-of-two arithmetic value mismatch");
            ++checks;
        }
    }
    for(const std::string op:{"div","mod","sdiv","smod"})for(uint64_t divisor:{0ull,3ull,8ull}){
        if((op=="div"||op=="mod")&&divisor==8)continue;
        Graph graph;auto input=graph.wire(8,"x","input");auto output=graph.add(op,8,input,constant(divisor,8));graph.optimize();
        if(graph.resolved(output)!=output)throw std::runtime_error("non-power/zero/signed arithmetic rewritten unsafely");
    }
    if(Graph::calculate("sdiv",uint8_t(-5),8,8)!=0||uint8_t(Graph::calculate("smod",uint8_t(-5),8,8))!=uint8_t(-5))throw std::runtime_error("signed truncation oracle changed");
    std::cout<<"PASS power-of-two graph arithmetic: "<<checks<<" numeric checks; eager/late aliases, gated constants, serialized graphs, mixed widths, unsigned div/mod/multiply, signed/zero/non-power preservation\n";
}
