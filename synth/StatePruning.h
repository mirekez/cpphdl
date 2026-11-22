#pragma once
#include "cpphdl_graph.h"
#include <unordered_map>

namespace cpphdl::synth {
struct SynchronousRead {
    size_t memory;
    graph::Value address, enable;
};

// Use the same recognition for pruning and RTL spelling: an inferred RAM's
// complete output register must survive even when only some bits are used.
inline std::optional<SynchronousRead> synchronousRead(graph::Graph& g, const graph::State& s) {
    using namespace graph;
    if (!s.reset.empty() || s.bits.empty() || s.next.size()!=s.bits.size()) return {};
    std::optional<SynchronousRead> match;
    for (size_t i=0;i<s.next.size();++i) {
        auto bit=g.resolve(s.next[i]);
        Value enable{1};
        if (bit>1 && g.nodes[Graph::owner(bit)].op=="mux") {
            const auto& mux=g.nodes[Graph::owner(bit)];auto lane=Graph::lane(bit);
            if (lane>=mux.left.size() || lane>=mux.right.size() ||
                g.resolve(mux.right[lane])!=g.resolve(s.bits[i])) return {};
            enable=g.resolved(mux.select);bit=g.resolve(mux.left[lane]);
        }
        if (bit<2) return {};
        const auto& read=g.nodes[Graph::owner(bit)];
        if (read.op!="memory_read") return {};
        auto memory=number(g.resolved(read.right)),word=number(g.resolved(read.select));
        if (!memory || *memory>=g.memories.size() || !word ||
            g.memories[*memory].width!=s.bits.size() || *word*64+Graph::lane(bit)!=i ||
            *word*64>=s.bits.size() || read.width!=std::min<size_t>(64,s.bits.size()-*word*64)) return {};
        auto address=g.resolved(read.left);
        if (!match) match=SynchronousRead{size_t(*memory),address,enable};
        else if (match->memory!=*memory || match->address!=address || match->enable!=enable) return {};
    }
    return match;
}

// Final-emission pass, after graph validation and hard-block linking. Keep
// arithmetic dependencies conservative; only muxes and bitwise operations
// have independent lanes. Scheduling metadata is deliberately not a root.
inline size_t pruneUnusedStateBits(graph::Graph& g) {
    using namespace graph;
    if (g.states.empty()) return 0;
    using Owner=std::pair<size_t,size_t>;
    std::unordered_map<Bit,std::vector<Owner>> owners;
    for (size_t i=0;i<g.states.size();++i)
        for (size_t lane=0;lane<g.states[i].bits.size();++lane) {
            auto bit=g.resolve(g.states[i].bits[lane]);
            // Do not reinterpret unusual state aliases as ordinary registers.
            if (bit<2 || g.nodes[Graph::owner(bit)].op!="state") return 0;
            owners[bit].push_back({i,lane});
        }
    std::vector<uint64_t> live(g.nodes.size());
    std::vector<std::vector<bool>> stateLive;
    for (const auto& s:g.states) stateLive.emplace_back(s.bits.size(),false);
    std::vector<Bit> pending;
    auto requireBit=[&](Bit raw) {
        auto bit=g.resolve(raw);
        if (bit<2) return;
        auto index=Graph::owner(bit);auto lane=Graph::lane(bit);
        if (index>=g.nodes.size() || lane>=g.nodes[index].width)
            throw std::runtime_error("invalid register liveness dependency");
        auto mask=uint64_t(1)<<lane;
        if (!(live[index]&mask)) {live[index]|=mask;pending.push_back(bit);}
    };
    auto require=[&](const Value& value) {for (auto bit:value) requireBit(bit);};
    for (const auto& p:g.ports) if (!p.input) require(p.bits);
    for (const auto& r:g.replacements) for (const auto& [name,bits]:r.pins) require(bits);
    for (const auto& a:g.memoryAccesses) {require(a.address);require(a.enabled);}
    for (const auto& w:g.memoryWrites) {require(w.address);require(w.data);require(w.enabled);}
    for (size_t i=0;i<g.nodes.size();++i)
        if (g.nodes[i].hostEffect() || g.nodes[i].validation())
            for (unsigned lane=0;lane<g.nodes[i].width;++lane) requireBit((i+1)*64+2+lane);
    for (const auto& s:g.states) if (synchronousRead(g,s)) require(s.bits);
    while (!pending.empty()) {
        auto bit=pending.back();pending.pop_back();
        const auto& n=g.nodes[Graph::owner(bit)];auto lane=Graph::lane(bit);
        if (n.op=="state") {
            auto found=owners.find(bit);
            if (found==owners.end()) continue; // Linked replacement output.
            for (auto [group,offset]:found->second) if (!stateLive[group][offset]) {
                stateLive[group][offset]=true;const auto& s=g.states[group];
                requireBit(s.next[offset]);require(s.trigger);require(s.reset);
                if (!s.resetValue.empty()) requireBit(s.resetValue[offset]);
            }
        } else if (n.op=="input") {
            continue;
        } else if (n.op=="mux" || n.op=="and" || n.op=="or" || n.op=="xor" || n.op=="not") {
            require(n.select);
            if (lane<n.left.size()) requireBit(n.left[lane]);else require(n.left);
            if (lane<n.right.size()) requireBit(n.right[lane]);else require(n.right);
        } else {
            require(n.left);require(n.right);require(n.select);
        }
    }
    size_t removed=0,retained=0;
    for (size_t i=0;i<g.states.size();++i) {
        auto& s=g.states[i];Value bits,next,resetValue;
        for (size_t lane=0;lane<s.bits.size();++lane) {
            if (stateLive[i][lane]) {
                bits.push_back(s.bits[lane]);next.push_back(s.next[lane]);
                if (!s.resetValue.empty()) resetValue.push_back(s.resetValue[lane]);
            } else {
                // Unobserved sibling lanes can still appear in word-level
                // producers. Tie those lanes off instead of leaving dangling
                // references, while preserving IDs of every retained bit.
                g.connect({s.bits[lane]},{0});++removed;
            }
        }
        if (bits.empty()) continue;
        s.bits=std::move(bits);s.next=std::move(next);s.resetValue=std::move(resetValue);
        if (retained!=i) g.states[retained]=std::move(s);
        ++retained;
    }
    g.states.resize(retained);
    return removed;
}
}
