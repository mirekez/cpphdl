#pragma once
#include "cpphdl_graph.h"

// Independent bit-addressed evaluator for mapper/exporter unit tests.
struct Evaluate {
    cpphdl::graph::Graph g;
    std::vector<size_t> order;
    std::vector<uint8_t> bits;
    explicit Evaluate(cpphdl::graph::Graph graph) : g(std::move(graph)), order(g.dependencyOrder()), bits((g.nodes.size()+2)*64) {}
    uint64_t get(cpphdl::graph::Value v) {
        uint64_t result = 0;
        for(size_t i=0; i<v.size() && i<64; ++i) {
            auto b=g.resolve(v[i]); if(b<2 ? b : bits[b]) result |= uint64_t(1)<<i;
        }
        return result;
    }
    void set(cpphdl::graph::Value v,uint64_t n) {
        for(size_t i=0;i<v.size();++i) bits[g.resolve(v[i])] = i<64 && ((n>>i)&1);
    }
    void input(const std::string& name,uint64_t n) {
        for(auto& p:g.ports) if(p.name==name && p.input) { set(p.bits,n); return; }
        throw std::runtime_error("missing input "+name);
    }
    uint64_t output(const std::string& name) {
        for(auto& p:g.ports) if(p.name==name && !p.input) return get(p.bits);
        throw std::runtime_error("missing output "+name);
    }
    void eval() {
        for(auto i:order) {
            auto& n=g.nodes[i]; if(n.op=="input" || n.op=="state") continue;
            uint64_t a=get(n.left),b=get(n.right),v=0;
            if(n.op=="mux") v=get(n.select)?a:b;
            else if(n.op=="and") v=a&b;
            else if(n.op=="or") v=a|b;
            else if(n.op=="xor") v=a^b;
            else if(n.op=="any") v=a!=0;
            else if(n.op=="all") v=a==cpphdl::graph::mask(n.left.size());
            else if(n.op=="parity") v=__builtin_parityll(a);
            else v=cpphdl::graph::Graph::calculate(n.op,a,b,n.left.size());
            cpphdl::graph::Value target; for(unsigned j=0;j<n.width;++j) target.push_back((i+1)*64+2+j);
            set(target,v);
        }
    }
    void tick() {
        eval(); auto previous=bits;
        for(auto& s:g.states) for(size_t i=0;i<s.bits.size();++i) {
            auto b=g.resolve(s.next[i]); bits[g.resolve(s.bits[i])]=b<2?b:previous[b];
        }
        eval();
    }
};
