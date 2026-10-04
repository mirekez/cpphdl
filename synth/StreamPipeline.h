#pragma once
#include "cpphdl_graph.h"

namespace cpphdl::synth {
// Rebuild a streaming region from its logical transition, never from its hold
// muxes. HLS supplies stage boundaries; synthesis optionally supplies delays.
inline void buildStreamPipeline(graph::Graph& out, graph::StreamPipeline& region,
    graph::Graph logic, const std::function<double(const graph::Node&)>& delay = {},
    double budget = 0, double clockToQ = 0) {
    using namespace graph;
    if (!region.stages || region.stages > 64)
        throw std::runtime_error("invalid streaming HLS stage count");
    for (const auto& state : logic.states)
        if (state.resetValue.size() != state.bits.size() ||
            std::any_of(state.resetValue.begin(),state.resetValue.end(),[](Bit b) { return b > 1; }))
            throw std::runtime_error("streaming state requires a constant full-width initializer");
    logic.optimize();
    auto order = logic.dependencyOrder();
    const auto scope = region.scope + ".schedule_" + std::to_string(region.generation++);
    out.currentScope = scope;
    region.registers.clear();
    auto advance = out.wire(1,scope + ".advance");
    auto reg = [&](unsigned width, const std::string& name) {
        auto q = out.wire(width,scope + "." + name,"state");
        region.registers.push_back(q);
        return q;
    };
    auto next = [&](const Value& q, const Value& d, Value resetValue = {}) {
        if (resetValue.empty()) resetValue.resize(q.size(),0);
        return out.mux(region.pins.at("reset"),resetValue,out.mux(advance,d,q));
    };
    struct Producer {
        unsigned base = 0, stage = 0;
        double arrival = 0;
        std::vector<Value> versions;
    };
    std::vector<Producer> producers(logic.nodes.size());
    auto bind = [&](const Value& from, const Value& to) {
        for (size_t i = 0; i < from.size(); ++i) {
            auto b = logic.resolve(from[i]);
            if (b < 2) continue;
            auto& p = producers.at(Graph::owner(b));
            if (p.versions.empty()) p.versions.push_back(Value(logic.nodes[Graph::owner(b)].width));
            p.versions[0][Graph::lane(b)] = to.at(i);
        }
    };
    for (const auto& port : logic.ports) if (port.input)
        bind(port.bits,out.resolved(region.pins.at(port.name)));
    std::vector<Value> feedback;
    for (const auto& state : logic.states) {
        auto q = reg(state.bits.size(),"feedback_" + std::to_string(feedback.size()));
        feedback.push_back(q); bind(state.bits,q);
        for (auto b : state.bits) producers[Graph::owner(b)].arrival = clockToQ;
    }
    auto align = [&](const Value& value, unsigned stage) {
        Value result;
        for (auto bit : logic.resolved(value)) {
            if (bit < 2) { result.push_back(bit); continue; }
            const auto n = Graph::owner(bit);
            auto& p = producers.at(n);
            if (p.versions.empty() || stage < p.stage)
                throw std::runtime_error("invalid streaming dependency schedule");
            while (p.stage + p.versions.size() <= stage) {
                auto q = reg(p.versions.back().size(),"stage_" +
                    std::to_string(p.stage+p.versions.size()) + "_value_" + std::to_string(n));
                out.states.push_back({q,next(q,p.versions.back()),{1}});
                p.versions.push_back(q);
            }
            result.push_back(p.versions[stage-p.stage][Graph::lane(bit)]);
        }
        return result;
    };
    unsigned latency = std::max(region.stages,region.latency);
    auto minimumStage = [&](const Value& value, unsigned base) {
        unsigned stage = base;
        for (auto b : logic.resolved(value)) if (b > 1) {
            const auto& p = producers.at(Graph::owner(b));
            if (base < p.base) throw std::runtime_error("backwards HLS stage boundary");
            stage = std::max(stage,p.stage + base-p.base);
        }
        return stage;
    };
    for (auto n : order) {
        const auto& node = logic.nodes[n];
        if (node.op == "input" || node.op == "state") continue;
        if (node.op == "memory_read" || node.hostEffect())
            throw std::runtime_error("streaming memory/host effects require an explicit schedule");
        const auto pos = node.scope.rfind(".hls_stage_");
        if (pos == std::string::npos) throw std::runtime_error("missing HLS stage boundary");
        const unsigned base = std::stoul(node.scope.substr(pos+11));
        unsigned stage = std::max({minimumStage(node.left,base),minimumStage(node.right,base),minimumStage(node.select,base)});
        double incoming = 0, cost = delay ? delay(node) : 0;
        for (auto* operand : {&node.left,&node.right,&node.select})
            for (auto b : logic.resolved(*operand)) if (b > 1) {
                const auto& p = producers[Graph::owner(b)];
                incoming = std::max(incoming,stage > p.stage ? clockToQ : p.arrival);
            }
        if (delay && incoming + cost > budget + 1e-9) {
            if (clockToQ + cost > budget + 1e-9)
                throw std::runtime_error("streaming cell cannot fit target period");
            ++stage; incoming = clockToQ;
        }
        auto a = align(node.left,stage), b = align(node.right,stage), s = align(node.select,stage);
        out.currentScope = scope + ".stage_" + std::to_string(stage);
        producers[n] = {base,stage,incoming+cost,{out.add(node.op,node.width,a,b,s,node.name)}};
    }
    for (const auto& port : logic.ports) if (!port.input)
        latency = std::max(latency,minimumStage(port.bits,region.stages));
    for (const auto& state : logic.states)
        latency = std::max(latency,1+minimumStage(state.next,region.stages-1));
    out.currentScope = scope;
    std::vector<Value> valid;
    for (unsigned i = 0; i < latency; ++i) valid.push_back(reg(1,"valid_" + std::to_string(i)));
    auto ready = out.binary("or",out.unary("not",valid.back()),region.pins.at("response_ready_in"),1);
    out.connect(advance,ready);
    for (unsigned i = 0; i < latency; ++i)
        out.states.push_back({valid[i],next(valid[i],i ? valid[i-1] : region.pins.at("command_valid_in")),{1}});
    for (const auto& port : logic.ports) if (!port.input)
        region.pins[port.name] = align(port.bits,latency);
    for (size_t i = 0; i < logic.states.size(); ++i) {
        const auto& state = logic.states[i];
        auto d = align(state.next,latency-1);
        auto enable = latency == 1 ? region.pins.at("command_valid_in") : valid[latency-2];
        out.states.push_back({feedback[i],next(feedback[i],out.mux(enable,d,feedback[i]),state.resetValue),{1}});
    }
    region.pins["command_ready_out"] = ready;
    region.pins["response_valid_out"] = valid.back();
    region.latency = latency;
    out.currentScope = region.scope;
}
}
