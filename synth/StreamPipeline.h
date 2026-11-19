#pragma once
#include "cpphdl_graph.h"
#include "AtomicMethod.h"
#include "Mapping.h"

namespace cpphdl::synth {
// Rebuild a streaming region from its logical transition, never from its hold
// muxes. HLS supplies stage boundaries; synthesis optionally supplies delays.
inline void buildStreamPipeline(graph::Graph& out, graph::StreamPipeline& region,
    graph::Graph logic, const std::function<double(const graph::Node&)>& delay = {},
    double budget = 0, double clockToQ = 0,
    const std::map<std::string, double>& inputArrival = {}) {
    using namespace graph;
    if (!region.stages || region.stages > 64)
        throw std::runtime_error("invalid streaming HLS stage count");
    for (const auto& state : logic.states)
        if (state.resetValue.size() != state.bits.size() ||
            std::any_of(state.resetValue.begin(),state.resetValue.end(),[](Bit b) { return b > 1; }))
            throw std::runtime_error("streaming state requires a constant full-width initializer");
    logic.optimize();
    const bool singleVisit=std::any_of(logic.attributes.begin(),logic.attributes.end(),
        [&](const ScopeAttribute& a){return a.scope==region.scope && a.name=="hls_single_visit_inputs" && a.value=="1";});
    auto order = logic.dependencyOrder();
    const auto scope = region.scope + ".schedule_" + std::to_string(region.generation++);
    // Rebuild the complete call subtree. A source function may itself contain
    // source calls and atomic replacements; retaining grandchildren would
    // leave stale pipeline instances after retiming an ancestor.
    std::set<std::string> stale;
    std::vector<std::string> parents{region.scope};
    for (size_t i=0;i<parents.size();++i)
        for (const auto& attr : out.attributes)
            if (attr.name == "atomic_parent" && attr.value == parents[i] && stale.insert(attr.scope).second)
                parents.push_back(attr.scope);
    out.pipelines.erase(std::remove_if(out.pipelines.begin(),out.pipelines.end(),
        [&](const StreamPipeline& p){return stale.count(p.scope);}),out.pipelines.end());
    out.attributes.erase(std::remove_if(out.attributes.begin(),out.attributes.end(),
        [&](const ScopeAttribute& a){return stale.count(a.scope);}),out.attributes.end());
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
    // A source region with one outstanding invocation can retain each value
    // after its first timing capture. Qualify that capture with the token at
    // the producer's stage, so later bubbles cannot overwrite it. Bind these
    // wires after scheduling has established the complete valid pipeline.
    std::map<unsigned,Value> stageValid;
    auto validAt=[&](unsigned stage) {
        if(!stage)return region.pins.at("command_valid_in");
        auto& value=stageValid[stage];
        if(value.empty())value=out.wire(1,scope+".token_at_"+std::to_string(stage));
        return value;
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
    for (const auto& port : logic.ports) if (port.input) {
        bind(port.bits,out.resolved(region.pins.at(port.name)));
        auto arrival = inputArrival.find(port.name);
        if (arrival != inputArrival.end()) for (auto b : logic.resolved(port.bits)) if (b > 1)
            producers[Graph::owner(b)].arrival = std::max(producers[Graph::owner(b)].arrival, arrival->second);
    }
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
                // This proof applies only to one outstanding invocation. Keep
                // the first timing capture and reuse it at later alignments;
                // ordinary overlapping pipelines still shift every token.
                if(singleVisit && p.versions.size()>1) {
                    p.versions.push_back(p.versions[1]);continue;
                }
                auto q = reg(p.versions.back().size(),"stage_" +
                    std::to_string(p.stage+p.versions.size()) + "_value_" + std::to_string(n));
                auto data=singleVisit?out.mux(validAt(p.stage),p.versions.back(),q):p.versions.back();
                out.states.push_back({q,next(q,data),{1}});
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
    std::map<std::string,std::pair<unsigned,Value>> atomicResults;
    for (auto n : order) {
        const auto& node = logic.nodes[n];
        if (node.op == "input" || node.op == "state") continue;
        if (node.op == "memory_read" || node.op == "external_load" || node.op == "external_store" || node.op == "external_fault" || node.op == "local_load" || node.op == "local_store" || node.op == "local_fault" || node.hostEffect())
            throw std::runtime_error("streaming memory/host effects require an explicit schedule");
        const auto pos = node.scope.rfind(".hls_stage_");
        if (pos == std::string::npos) throw std::runtime_error("missing HLS stage boundary");
        const unsigned base = std::stoul(node.scope.substr(pos+11));
        unsigned stage = std::max({minimumStage(node.left,base),minimumStage(node.right,base),minimumStage(node.select,base)});
        if (node.op == "atomic_call" || node.op == "pipeline_call") {
            auto found=atomicResults.find(node.name);
            if (found == atomicResults.end()) {
                std::optional<AtomicMethod> definition;
                for (const auto& a : logic.attributes) if (a.name == "atomic_method" && a.scope == node.name)
                    definition = AtomicMethod::parse(a.value);
                if (!definition) throw std::runtime_error("missing nested atomic method: "+node.name);
                const auto& spec=*definition;
                auto inputs=align(node.left,stage);
                if (inputs.size()!=3*spec.argumentBits) throw std::runtime_error("atomic input width mismatch");
                Graph childLogic; childLogic.load(spec.logic.c_str());
                if (delay) {
                    std::set<size_t> expand;
                    for (auto id : childLogic.dependencyOrder()) {
                        const auto& op=childLogic.nodes[id].op;
                        if (op!="input" && op!="state" && op!="atomic_call" && op!="pipeline_call" && op!="blackbox" &&
                            op!="and" && op!="or" && op!="xor" && op!="mux") expand.insert(id);
                    }
                    if (!expand.empty()) childLogic=expandGates(std::move(childLogic),expand);
                }
                StreamPipeline child; child.scope=scope+"."+node.name; child.logic=spec.logic;
                child.latency=spec.minimumLatency;
                child.pins["reset"]=region.pins.at("reset");
                // Pure nested C++ calls inherit the parent's one-in-flight
                // proof. Admit their token when its arguments reach this
                // stage, allowing their own alignment storage to be reused.
                // Hard-block replacements retain their existing free-running
                // arithmetic/selected-latency protocol.
                if(singleVisit && spec.module.empty()) {
                    childLogic.attributes.push_back({child.scope,"hls_single_visit_inputs","1"});
                    child.pins["command_valid_in"]=validAt(stage);
                } else child.pins["command_valid_in"]={1};
                // Free-running children fill before the parent can stall;
                // then response_ready=advance freezes both together. A
                // single-visit child instead holds its completed data through
                // later parent stages and stalls, until the next invocation.
                child.pins["response_ready_in"]=advance;
                unsigned offset=0;
                for (const auto& pin : {"operation_in","index_in","value_in"}) {
                    child.pins[pin]=slice(inputs,offset,spec.argumentBits); offset+=spec.argumentBits;
                }
                std::map<std::string,double> arrivals;
                double incoming=clockToQ;
                for (auto bit : logic.resolved(node.left)) if (bit>1) {
                    const auto& p=producers[Graph::owner(bit)];
                    incoming=std::max(incoming,stage>p.stage?clockToQ:p.arrival);
                }
                for (const auto& pin : {"operation_in","index_in","value_in"}) arrivals[pin]=incoming;
                buildStreamPipeline(out,child,std::move(childLogic),delay,budget,clockToQ,arrivals);
                region.registers.insert(region.registers.end(),child.registers.begin(),child.registers.end());
                Value result=child.pins.at("result_out");
                const auto& fault=child.pins.at("fault_out"); result.insert(result.end(),fault.begin(),fault.end());
                if (!spec.module.empty()) {
                out.attributes.push_back({child.scope,"pipeline_replacement",spec.module});
                out.attributes.push_back({child.scope,"pipeline_replacement_body",spec.body});
                out.attributes.push_back({child.scope,"pipeline_replacement_min_latency",std::to_string(spec.minimumLatency)});
                }
                out.attributes.push_back({child.scope,"atomic_parent",region.scope});
                found=atomicResults.emplace(node.name,std::make_pair(stage+child.latency,std::move(result))).first;
                out.pipelines.push_back(std::move(child));
            }
            auto offset=number(node.right);
            if (!offset || *offset+node.width>found->second.second.size()) throw std::runtime_error("atomic output width mismatch");
            producers[n]={base,found->second.first,clockToQ,{slice(found->second.second,*offset,node.width)}};
            latency=std::max(latency,found->second.first);
            continue;
        }
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
    for(const auto& [stage,value]:stageValid)out.connect(value,valid.at(stage-1));
    region.pins["command_ready_out"] = ready;
    region.pins["response_valid_out"] = valid.back();
    region.latency = latency;
    out.currentScope = region.scope;
}
}
