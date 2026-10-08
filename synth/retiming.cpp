#include "retiming.h"
#include "KeepBoxes.h"
#include "Mapping.h"
#include "StreamPipeline.h"
#include <cmath>
#include <tuple>

namespace cpphdl::synth {
using namespace graph;
namespace {
Value nodeBits(const Graph& g, size_t n) {
    Value v(g.nodes.at(n).width);
    for (unsigned i = 0; i < v.size(); ++i) v[i] = (n + 1) * 64 + 2 + i;
    return v;
}
bool inside(const std::string& name, const std::string& scope) {
    return scope.empty() || name == scope || name.find(scope + ".") == 0 || name.find(scope + ":") == 0;
}
bool selected(const Graph& g, const State& s, const RetimingRule& rule) {
    return inside(g.nodes.at(Graph::owner(s.bits.at(0))).name, rule.scope);
}
std::map<Bit, std::pair<size_t, size_t>> stateBits(const Graph& g) {
    std::map<Bit, std::pair<size_t, size_t>> result;
    for (size_t i = 0; i < g.states.size(); ++i)
        for (size_t b = 0; b < g.states[i].bits.size(); ++b) result[g.states[i].bits[b]] = {i, b};
    return result;
}
bool sameDomain(const State& a, const State& b) {
    return a.clock == b.clock && a.falling == b.falling && a.reset == b.reset && a.trigger == b.trigger;
}
bool operation(const Node& n) {
    return n.op != "state" && n.op != "input" && n.op != "wire" && n.op != "memory_read" && n.op != "blackbox" && !n.hostEffect();
}
Value copyOperation(Graph& g, const Node& n, Value left, Value right, Value select) {
    auto previousScope = g.currentScope;
    g.currentScope = n.scope;
    auto result = g.add(n.op, n.width, std::move(left), std::move(right), std::move(select), n.name);
    g.currentScope = std::move(previousScope);
    return result;
}
void pruneStates(Graph& g) {
    auto owners = stateBits(g);
    std::set<size_t> liveStates, visited;
    std::function<void(Value)> visit = [&](Value value) {
        for (auto bit : g.resolved(value)) if (bit > 1) {
            auto state = owners.find(bit);
            if (state != owners.end()) {
                auto id = state->second.first;
                if (liveStates.insert(id).second) { visit(g.states[id].next); visit(g.states[id].reset); }
            } else if (visited.insert(Graph::owner(bit)).second) {
                auto node = g.nodes.at(Graph::owner(bit));
                visit(node.left); visit(node.right); visit(node.select);
            }
        }
    };
    for (const auto& p : g.ports) if (!p.input) visit(p.bits);
    for (const auto& w : g.memoryWrites) { visit(w.address); visit(w.data); visit(w.enabled); }
    for (const auto& a : g.memoryAccesses) { visit(a.address); visit(a.enabled); }
    std::vector<State> kept;
    for (auto i : liveStates) kept.push_back(g.states[i]);
    g.states = std::move(kept);
}
bool forward(Graph& g, size_t index, const RetimingRule& rule) {
    const auto node = g.nodes[index];
    if (!operation(node) || !inside(node.scope, rule.scope) || g.resolved(nodeBits(g, index)) != nodeBits(g, index)) return false;
    auto owners = stateBits(g);
    std::set<size_t> inputs;
    for (const auto* v : {&node.left, &node.right, &node.select}) for (auto bit : g.resolved(*v)) if (bit > 1) {
        auto it = owners.find(bit);
        if (it == owners.end() || !selected(g, g.states[it->second.first], rule)) return false;
        inputs.insert(it->second.first);
    }
    if (inputs.empty()) return false;
    State domain = g.states[*inputs.begin()];
    for (auto i : inputs) if (!sameDomain(domain, g.states[i])) return false;
    auto substitute = [&](Value value, bool reset) {
        value = g.resolved(value);
        for (auto& bit : value) if (bit > 1) {
            auto [s, b] = owners.at(bit);
            bit = reset ? g.states[s].resetValue.at(b) : g.states[s].next.at(b);
        }
        return value;
    };
    Value resetValue;
    if (!domain.reset.empty()) {
        resetValue = copyOperation(g, node, substitute(node.left, true), substitute(node.right, true), substitute(node.select, true));
        g.simplifyLast(resetValue); resetValue = g.resolved(resetValue);
        for (auto bit : resetValue) if (bit > 1) return false;
    }
    auto next = copyOperation(g, node, substitute(node.left, false), substitute(node.right, false), substitute(node.select, false));
    auto initialInput = [&](Value v) {
        v = g.resolved(v);
        for (auto& bit : v) if (bit > 1) {
            const auto& source = g.nodes.at(Graph::owner(bit));
            bit = source.left.empty() ? 0 : source.left.at(Graph::lane(bit));
        }
        return v;
    };
    auto initial = copyOperation(g, node, initialInput(node.left), initialInput(node.right), initialInput(node.select));
    g.simplifyLast(initial); initial = g.resolved(initial);
    if (std::any_of(initial.begin(), initial.end(), [](Bit b) { return b > 1; })) return false;
    auto bits = g.wire(node.width, node.scope + ".retimed_forward", "state");
    g.nodes.back().left = initial;
    g.states.push_back({bits, next, {1}, domain.clock, domain.falling, domain.reset, resetValue});
    g.connect(nodeBits(g, index), bits);
    pruneStates(g);
    return true;
}
bool backward(Graph& g, size_t index, const RetimingRule& rule) {
    const State state = g.states[index];
    if (!selected(g, state, rule) || !state.reset.empty()) return false;
    auto next = g.resolved(state.next);
    if (next.empty()) return false;
    std::optional<size_t> producer;
    for (auto bit : next) if (bit > 1) {
        auto candidate = Graph::owner(bit);
        if (g.resolved(nodeBits(g, candidate)) == next) { producer = candidate; break; }
    }
    if (!producer) return false;
    auto id = *producer;
    const auto node = g.nodes[id];
    if (!operation(node) || !inside(node.scope, rule.scope)) return false;
    // Preserve the native model's initial value. Only use an inverse when it
    // is exact, then verify it by constant evaluation of the original operator.
    auto zero = [&](Value v) { v = g.resolved(v); for (auto& b : v) if (b > 1) b = 0; return v; };
    auto leftInit = zero(node.left), rightInit = zero(node.right), selectInit = zero(node.select);
    Value target;
    for (auto bit : state.bits) {
        const auto& init = g.nodes[Graph::owner(bit)].left;
        target.push_back(init.empty() ? 0 : init.at(Graph::lane(bit)));
    }
    auto matches = [&]() {
        auto initial = copyOperation(g, node, leftInit, rightInit, selectInit);
        g.simplifyLast(initial); return g.resolved(initial) == target;
    };
    if (!matches()) {
        auto lhs = g.resolved(node.left);
        auto desired = number(target), rhs = number(rightInit);
        if (!desired || !rhs || lhs.empty() || std::any_of(lhs.begin(), lhs.end(), [](Bit b) { return b < 2; })) return false;
        if (node.op == "add") leftInit = constant(*desired - *rhs, lhs.size());
        else if (node.op == "sub") leftInit = constant(*desired + *rhs, lhs.size());
        else if (node.op == "xor") leftInit = constant(*desired ^ *rhs, lhs.size());
        else return false;
        if (!matches()) return false;
    }
    g.states.erase(g.states.begin() + index);
    auto bank = [&](Value v, const Value& init) {
        v = g.resolved(v);
        if (std::all_of(v.begin(), v.end(), [](Bit b) { return b < 2; })) return v;
        auto bits = g.wire(v.size(), node.scope + ".retimed_backward", "state");
        for (unsigned offset = 0; offset < bits.size(); offset += 64)
            g.nodes[Graph::owner(bits[offset])].left = slice(init, offset, std::min<size_t>(64, bits.size() - offset));
        g.states.push_back({bits, v, {1}, state.clock, state.falling});
        return bits;
    };
    auto left = bank(node.left, leftInit), right = bank(node.right, rightInit), select = bank(node.select, selectInit);
    auto result = copyOperation(g, node, left, right, select);
    g.connect(state.bits, result);
    pruneStates(g);
    return true;
}

struct Scheduled { Value bits; unsigned stage = 0; double arrival = 0; };
class Pipeline {
    Graph& g;
    const RetimingRule& rule;
    const DelayModel& model;
    std::vector<State> original;
    std::map<Bit, std::pair<size_t, size_t>> owners;
    std::set<size_t> targets;
    std::map<size_t, Scheduled> nodes, states;
    std::set<size_t> visiting;
    KeepBoxes boxes;
    std::set<size_t> boxVisiting;
    std::map<std::tuple<Value, unsigned, Value>, Scheduled> delayed;
    State domain;
    Value syncReset;
    Value accept, commit;
    std::map<size_t, Value> resetValues, normalValues;
    double budget, capturedInputArrival = 0;
    bool alreadyFits;
    bool scheduledHls = false;
    Value cofactor(Value value, bool asserted, std::map<size_t, Value>& cache) {
        value = g.resolved(value);
        for (auto& bit : value) if (bit > 1) {
            if (Value{bit} == syncReset) { bit = asserted; continue; }
            auto index = Graph::owner(bit);
            if (boxes.owner.count(index)) continue;
            const auto node = g.nodes[index];
            if (node.op == "state" || node.op == "input") continue;
            if (!cache.count(index)) {
                auto left = cofactor(node.left, asserted, cache);
                auto right = cofactor(node.right, asserted, cache);
                auto select = cofactor(node.select, asserted, cache);
                auto result = copyOperation(g, node, left, right, select);
                g.simplifyLast(result); cache[index] = g.resolved(result);
            }
            bit = cache.at(index).at(Graph::lane(bit));
        }
        return value;
    }
public:
    unsigned inserted = 0, latency = 0;
    bool feedback = false;
    Pipeline(Graph& graph, const RetimingRule& r, const DelayModel& m, bool fits)
        : g(graph), rule(r), model(m), original(g.states), owners(stateBits(g)), boxes(g), alreadyFits(fits) {
        for (const auto& a : g.attributes) scheduledHls |= a.name == "scheduled_hls";
        for (size_t i = 0; i < original.size(); ++i) if (selected(g, original[i], rule)) targets.insert(i);
        if (targets.empty()) throw std::runtime_error("retiming scope has no registers");
        domain = original[*targets.begin()];
        for (auto i : targets) if (!sameDomain(domain, original[i])) throw std::runtime_error("fit pipeline requires one clock/edge/reset domain per rule");
        for (const auto& p : g.ports) if (p.input && (p.name == "work_reset" || (scheduledHls && p.name == "reset"))) syncReset = g.resolved(p.bits);
        if (!domain.reset.empty()) syncReset.clear();
        if (!syncReset.empty()) {
            std::map<size_t, Value> asserted, released;
            std::set<size_t> resetSeen;
            std::function<bool(Value)> dependsOnReset = [&](Value v) {
                for (auto bit : g.resolved(v)) if (bit > 1) {
                    if (Value{bit} == syncReset) return true;
                    const auto n = g.nodes[Graph::owner(bit)];
                    if (n.op != "state" && n.op != "input" && resetSeen.insert(Graph::owner(bit)).second &&
                        (dependsOnReset(n.left) || dependsOnReset(n.right) || dependsOnReset(n.select))) return true;
                }
                return false;
            };
            bool any = false, all = true, usesReset = false;
            for (auto s : targets) {
                auto reset = cofactor(original[s].next, true, asserted);
                auto normal = cofactor(original[s].next, false, released);
                usesReset |= dependsOnReset(original[s].next);
                bool constantReset = std::all_of(reset.begin(), reset.end(), [](Bit b) { return b < 2; });
                // Scheduled HLS deliberately does not reset RAM-like storage
                // registers. Holding those bits is an exact reset operation.
                if (scheduledHls && reset == original[s].bits) constantReset = true;
                any |= constantReset; all &= constantReset;
                resetValues[s] = reset; normalValues[s] = normal;
            }
            if (any && !all) throw std::runtime_error("fit pipeline requires consistent constant synchronous resets");
            if (!all && usesReset) throw std::runtime_error("fit pipeline requires constant synchronous reset values");
            if (!all) { syncReset.clear(); resetValues.clear(); normalValues.clear(); }
        }
        budget = rule.period - model.setup - (syncReset.empty() ? 0 : model.mux);
        if (budget <= model.clockToQ) throw std::runtime_error("target period is below register overhead");
        // Cyclic regions execute one original state transition per transaction.
        std::set<size_t> active, done;
        std::function<void(size_t)> checkState;
        auto dependencies = [&](Value pending) {
            std::set<size_t> seen, result;
            while (!pending.empty()) {
                auto bit = g.resolve(pending.back()); pending.pop_back();
                if (bit < 2) continue;
                if (owners.count(bit)) result.insert(owners.at(bit).first);
                else if (seen.insert(Graph::owner(bit)).second) {
                    const auto& n = g.nodes.at(Graph::owner(bit));
                    for (const auto* v : {&n.left, &n.right, &n.select}) pending.insert(pending.end(), v->begin(), v->end());
                }
            }
            return result;
        };
        checkState = [&](size_t s) {
            if (active.count(s)) { feedback = true; return; }
            if (!done.insert(s).second) return;
            active.insert(s);
            for (auto dependency : dependencies(original[s].next)) checkState(dependency);
            active.erase(s);
        };
        for (auto s : targets) checkState(s);
        if (feedback) {
            if (targets.size() != original.size())
                throw std::runtime_error("feedback scheduling requires the entire register region");
            if (!scheduledHls && (!g.memories.empty() || !g.memoryWrites.empty() || !g.memoryAccesses.empty()))
                throw std::runtime_error("feedback scheduling cannot yet move memory transactions");
            for (const auto& w : g.memoryWrites)
                if (w.clock != domain.clock || w.falling != domain.falling)
                    throw std::runtime_error("scheduled memory must share the FSM clock/edge");
            for (const auto& p : g.ports) if (p.name == "retiming_ready_out" || p.name == "retiming_commit_out")
                throw std::runtime_error("feedback scheduling port already exists");
            accept = g.wire(1, rule.scope + ".accept");
            commit = g.wire(1, rule.scope + ".commit");
            budget -= model.mux; // Atomic commit enable on original registers.
            // At most one new stage per live operation. Bound the phase
            // decoder before scheduling, including its path into input muxes.
            unsigned phaseWidth = 1;
            while ((uint64_t(1) << phaseWidth) <= g.nodes.size() + 1) ++phaseWidth;
            double decoder = 2 * model.gate + std::ceil(std::log2(double(std::max(2u, phaseWidth)))) * model.gate;
            capturedInputArrival = model.clockToQ + decoder + model.mux +
                ((!syncReset.empty() || !domain.reset.empty()) ? model.mux : 0);
        }
        std::function<bool(Value, std::set<size_t>&)> touches = [&](Value value, std::set<size_t>& seen) {
            for (auto bit : g.resolved(value)) if (bit > 1) {
                if (owners.count(bit)) { if (targets.count(owners.at(bit).first)) return true; }
                else if (seen.insert(Graph::owner(bit)).second) {
                    auto n = g.nodes.at(Graph::owner(bit));
                    if (touches(n.left, seen) || touches(n.right, seen) || touches(n.select, seen)) return true;
                }
            }
            return false;
        };
        for (const auto& w : g.memoryWrites) {
            if (feedback && scheduledHls) continue;
            std::set<size_t> seen;
            if (touches(w.address, seen) || touches(w.data, seen) || touches(w.enabled, seen))
                throw std::runtime_error("fit pipeline cannot delay state driving a memory write");
        }
        for (size_t s = 0; s < original.size(); ++s) if (!targets.count(s)) {
            std::set<size_t> seen;
            if (touches(original[s].next, seen) || touches(original[s].reset, seen))
                throw std::runtime_error("fit pipeline cannot delay state driving an unselected register");
        }
        for (const auto& p : g.ports) if (!p.input && !scheduledHls) for (auto bit : g.resolved(p.bits))
            if (bit > 1 && !owners.count(bit)) throw std::runtime_error("fit pipeline requires registered output boundaries");
    }
    Scheduled delay(Scheduled source, unsigned target, Value resetBits = {}, bool timingBoundary = false) {
        if (std::all_of(source.bits.begin(), source.bits.end(), [](Bit b) { return b < 2; })) { source.stage = target; return source; }
        // A serialized transition holds its state, inputs and memory until
        // commit. Earlier results remain valid once their stage has elapsed;
        // they need no shift register merely to wait for another branch.
        // Keep their real arrival delay. Only a timing cut resets that delay.
        if (feedback && !timingBoundary) { source.stage = target; return source; }
        while (source.stage < target) {
            auto key = std::make_tuple(source.bits, source.stage + 1, resetBits);
            if (delayed.count(key)) { source = delayed.at(key); continue; }
            auto bits = g.wire(source.bits.size(), rule.scope + ".pipeline", "state");
            auto next = source.bits;
            auto reset = resetBits.empty() ? Value(bits.size(), 0) : resetBits;
            if (!syncReset.empty()) next = g.mux(syncReset, reset, next);
            g.states.push_back({bits, next, {1}, domain.clock, domain.falling, domain.reset,
                domain.reset.empty() ? Value{} : reset});
            inserted += bits.size();
            source = {bits, source.stage + 1, model.clockToQ}; delayed[key] = source;
        }
        return source;
    }
    Scheduled value(Value bits) {
        bits = g.resolved(bits);
        unsigned stage = 0;
        for (auto b : bits) if (b > 1) stage = std::max(stage, node(Graph::owner(b)).stage);
        Scheduled result; result.stage = stage;
        for (auto b : bits) {
            if (b < 2) { result.bits.push_back(b); continue; }
            auto part = node(Graph::owner(b));
            if (feedback && part.stage < stage && part.arrival > model.clockToQ + 1e-9)
                part = delay(part, part.stage + 1, {}, true);
            else part = delay(part, stage);
            result.bits.push_back(part.bits.at(Graph::lane(b)));
            result.arrival = std::max(result.arrival, part.arrival);
        }
        return result;
    }
    Scheduled state(size_t index) {
        if (states.count(index)) return states.at(index);
        const auto& s = original[index];
        if (s.clock != domain.clock || s.falling != domain.falling) throw std::runtime_error("fit pipeline cannot cross clock/edge domains");
        if (!targets.count(index)) return {s.bits, 0, model.clockToQ};
        if (feedback) return {s.bits, 0, model.clockToQ};
        if (!visiting.insert(index).second) throw std::runtime_error("fit pipeline register cycle");
        auto data = syncReset.empty() ? g.resolved(s.next) : normalValues.at(index);
        auto scheduled = value(data);
        g.states[index].next = syncReset.empty() ? scheduled.bits : g.mux(syncReset, resetValues.at(index), scheduled.bits);
        states[index] = {s.bits, scheduled.stage, model.clockToQ};
        visiting.erase(index);
        return states[index];
    }
    Scheduled node(size_t index) {
        if (nodes.count(index)) return nodes.at(index);
        if (boxes.owner.count(index)) {
            auto b = boxes.owner.at(index);
            const auto& box = boxes.boxes[b];
            if (!boxVisiting.insert(b).second) throw std::runtime_error("combinational path leaves and re-enters keep box");
            auto inputs = value(box.inputs);
            if (box.delay + model.clockToQ > budget + 1e-9)
                throw std::runtime_error("indivisible keep box / one-clock function exceeds target period: " + box.scope);
            if (inputs.arrival + box.delay > budget + 1e-9) inputs = delay(inputs, inputs.stage + 1, {}, true);
            std::map<Bit, Bit> replacement;
            for (size_t i = 0; i < box.inputs.size(); ++i) replacement[box.inputs[i]] = inputs.bits[i];
            auto mapped = [&](Value v) {
                v = g.resolved(v);
                for (auto& bit : v) if (bit > 1) bit = replacement.at(bit);
                return v;
            };
            for (auto n : box.nodes) {
                const auto body = g.nodes[n];
                auto result = copyOperation(g, body, mapped(body.left), mapped(body.right), mapped(body.select));
                auto old = nodeBits(g, n);
                for (size_t i = 0; i < old.size(); ++i) if (g.resolve(old[i]) == old[i]) replacement[old[i]] = result[i];
                nodes[n] = {result, inputs.stage, inputs.arrival + box.delay};
            }
            boxVisiting.erase(b);
            return nodes.at(index);
        }
        const auto n = g.nodes[index];
        if (n.op == "state") {
            auto bits = nodeBits(g, index);
            auto s = owners.find(bits[0]);
            if (s == owners.end()) throw std::runtime_error("unowned pipeline state");
            auto result = state(s->second.first);
            result.bits = slice(result.bits, s->second.second, n.width);
            return nodes[index] = result;
        }
        if (n.op == "input") {
            auto bits = nodeBits(g, index);
            if (!feedback || bits == syncReset || bits == domain.reset) return nodes[index] = {bits, 0, 0};
            auto held = g.wire(n.width, rule.scope + ".transaction_input", "state");
            auto next = g.mux(accept, bits, held);
            if (!syncReset.empty()) next = g.mux(syncReset, Value(n.width, 0), next);
            g.states.push_back({held, next, {1}, domain.clock, domain.falling, domain.reset,
                domain.reset.empty() ? Value{} : Value(n.width, 0)});
            inserted += n.width;
            return nodes[index] = {g.mux(accept, bits, held), 0, capturedInputArrival};
        }
        auto left = value(n.left), right = value(n.right), select = value(n.select);
        auto stage = std::max({left.stage, right.stage, select.stage});
        if (feedback) {
            // An early branch has spare cycles. Register it before joining a
            // later branch so its delay does not consume the later stage.
            for (auto* operand : {&left, &right, &select})
                if (operand->stage < stage && operand->arrival > model.clockToQ + 1e-9)
                    *operand = delay(*operand, operand->stage + 1, {}, true);
        } else {
            left = delay(left, stage); right = delay(right, stage); select = delay(select, stage);
        }
        double cost = cellDelay(g, n, model);
        if (cost + model.clockToQ > budget + 1e-9) throw std::runtime_error("indivisible cell exceeds target period: " + n.op);
        if (n.op == "memory_read" && stage && !(feedback && scheduledHls)) throw std::runtime_error("fit pipeline cannot delay a memory read address");
        double arrival = std::max({left.arrival, right.arrival, select.arrival}) + cost;
        if (arrival > budget + 1e-9) {
            if (n.op == "memory_read" && !(feedback && scheduledHls)) throw std::runtime_error("memory access exceeds target period; cannot move its transaction");
            if (feedback) {
                // Held transaction values can be registered as soon as they
                // settle, independently of a later reconverging branch.
                for (auto* operand : {&left, &right, &select})
                    if (operand->arrival + cost > budget + 1e-9)
                        *operand = delay(*operand, operand->stage + 1, {}, true);
                stage = std::max({left.stage, right.stage, select.stage});
            } else {
                ++stage;
                left = delay(left, stage, {}, true); right = delay(right, stage, {}, true); select = delay(select, stage, {}, true);
            }
            arrival = std::max({left.arrival, right.arrival, select.arrival}) + cost;
        }
        auto bits = copyOperation(g, n, left.bits, right.bits, select.bits);
        return nodes[index] = {bits, stage, arrival};
    }
    void run() {
        if (feedback) { runFeedback(); return; }
        for (auto s : targets) state(s);
        // Balance only external copies. Moving an internal bank here would
        // change consumers already scheduled against that bank's old latency.
        std::set<size_t> endpoints;
        for (const auto& p : g.ports) if (!p.input) for (auto b : g.resolved(p.bits)) if (owners.count(b) && targets.count(owners.at(b).first))
            endpoints.insert(owners.at(b).first);
        for (auto s : endpoints.empty() ? targets : endpoints) latency = std::max(latency, states.at(s).stage);
        std::map<size_t, Value> outputs;
        for (auto s : endpoints) {
            Value reset = !domain.reset.empty() ? original[s].resetValue : syncReset.empty() ? Value{} : resetValues.at(s);
            outputs[s] = delay(states.at(s), latency, reset).bits;
        }
        for (auto& p : g.ports) if (!p.input) for (auto& bit : p.bits) {
            auto owner = owners.find(g.resolve(bit));
            if (owner != owners.end() && outputs.count(owner->second.first)) bit = outputs.at(owner->second.first).at(owner->second.second);
        }
    }
    void runFeedback() {
        std::map<size_t, Scheduled> next;
        std::map<size_t, Scheduled> outputs;
        struct Write { Scheduled address, data, enabled; };
        std::vector<Write> writes;
        for (auto s : targets) if (!alreadyFits) {
            next[s] = value(syncReset.empty() ? original[s].next : normalValues.at(s));
            latency = std::max(latency, next[s].stage);
        }
        if (scheduledHls && !alreadyFits) {
            // Every output describes the same original FSM edge as its memory
            // writes and next-state registers. Present the bundle at commit.
            for (size_t p = 0; p < g.ports.size(); ++p) if (!g.ports[p].input) {
                outputs[p] = value(g.ports[p].bits);
                latency = std::max(latency, outputs[p].stage);
            }
            for (const auto& w : g.memoryWrites) {
                writes.push_back({value(w.address), value(w.data), value(w.enabled)});
                const auto& n = writes.back();
                latency = std::max({latency, n.address.stage, n.data.stage, n.enabled.stage});
            }
            for (const auto& [p, output] : outputs) g.ports[p].bits = delay(output, latency).bits;
            for (size_t i = 0; i < writes.size(); ++i) {
                g.memoryWrites[i].address = delay(writes[i].address, latency).bits;
                g.memoryWrites[i].data = delay(writes[i].data, latency).bits;
                g.memoryWrites[i].enabled = g.binary("and", delay(writes[i].enabled, latency).bits, commit, 1);
            }
            // Read ports are combinational while a transaction is in flight.
            // All writes wait for commit, so delayed reads see the old store.
            g.memoryAccesses.clear();
        }
        for (auto s : targets) if (!alreadyFits) {
            auto data = delay(next.at(s), latency).bits;
            data = g.mux(commit, data, original[s].bits);
            if (!syncReset.empty()) data = g.mux(syncReset, resetValues.at(s), data);
            g.states[s].next = data;
        }
        Value ready{1}, complete{1};
        if (latency) {
            unsigned width = 1;
            while ((uint64_t(1) << width) <= latency) ++width;
            auto phase = g.wire(width, rule.scope + ".transaction_phase", "state");
            ready = g.binary("eq", phase, constant(0, width), 1);
            complete = g.binary("eq", phase, constant(latency, width), 1);
            auto nextPhase = g.mux(complete, constant(0, width), g.binary("add", phase, constant(1, width), width));
            if (!syncReset.empty()) nextPhase = g.mux(syncReset, constant(0, width), nextPhase);
            g.states.push_back({phase, nextPhase, {1}, domain.clock, domain.falling, domain.reset,
                domain.reset.empty() ? Value{} : constant(0, width)});
            inserted += width;
        }
        Value reset = syncReset.empty() ? domain.reset : syncReset;
        if (!reset.empty()) {
            ready = g.mux(reset, {0}, ready);
            complete = g.mux(reset, {0}, complete);
        }
        g.connect(accept, ready); g.connect(commit, complete);
        g.ports.push_back({"retiming_ready_out", ready, false});
        g.ports.push_back({"retiming_commit_out", complete, false});
    }
};
}
RetimingReport retime(Graph& graph, const RetimingRule& rule, const DelayModel& model) {
    if (!std::isfinite(rule.period) || rule.period <= 0) throw std::runtime_error("retiming period must be positive");
    if (rule.mode != "keep_behaviour_retiming" && rule.mode != "fit_pipeline_retiming") throw std::runtime_error("unknown retiming mode: " + rule.mode);
    Graph g = graph; g.validateClocks(); g.optimize();
    if (g.clockContract == ClockContract::ExplicitEvents) throw std::runtime_error("retiming requires a synchronous clock contract");
    bool found = false;
    for (const auto& s : g.states) { if (s.trigger != Value{1}) throw std::runtime_error("retiming requires unconditional clock events"); found |= selected(g, s, rule); }
    if (!found) throw std::runtime_error("retiming scope has no registers: " + rule.scope);
    RetimingReport report; report.target = rule.period; report.before = estimateTiming(g, model, rule.scope).worst;
    bool streaming = false;
    for (const auto& attr : g.attributes) if (attr.name == "hls_pipeline_ii" &&
        (inside(attr.scope,rule.scope) || inside(rule.scope,attr.scope))) {
        bool metadata = false;
        for (const auto& p : g.pipelines) metadata |= p.scope == attr.scope;
        if (!metadata) throw std::runtime_error("streaming retiming requires transition metadata; regenerate this graph");
    }
    for (const auto& p : g.pipelines) {
        if (!inside(p.scope,rule.scope) && inside(rule.scope,p.scope))
            throw std::runtime_error("retiming cannot split a streaming region scope: " + p.scope);
        streaming |= inside(p.scope,rule.scope);
    }
    if (streaming) {
        if (report.before <= rule.period + 1e-9) {
            report.after = report.before; report.met = true; graph = std::move(g); return report;
        }
        if (rule.mode != "fit_pipeline_retiming")
            throw std::runtime_error("streaming feedback latency changes require fit_pipeline_retiming");
        const double budget = rule.period-model.setup-3*model.mux;
        if (budget <= model.clockToQ)
            throw std::runtime_error("streaming target period is below register/control overhead");
        for (auto& p : g.pipelines) if (inside(p.scope,rule.scope)) {
            if (estimateTiming(g,model,p.scope).worst <= rule.period + 1e-9) continue;
            // The region may be fed by combinational logic in its parent.
            // Its first stage has only the remaining timing budget available.
            const auto boundaryTiming = estimateTiming(g,model);
            std::map<std::string,double> inputArrival;
            for (const auto& [name,bits] : p.pins) for (auto bit : g.resolved(bits)) if (bit > 1)
                inputArrival[name] = std::max(inputArrival[name],boundaryTiming.arrival.at(Graph::owner(bit)));
            auto oldPins = p.pins;
            for (auto& [name,bits] : oldPins) bits = g.resolved(bits);
            for (auto& [name,bits] : p.pins) bits = g.resolved(bits);
            std::set<Value> owned;
            size_t oldBits = 0;
            for (auto bits : p.registers) { owned.insert(g.resolved(bits)); oldBits += bits.size(); }
            int clock = -1; bool falling = false, domainFound = false;
            for (const auto& state : g.states) if (owned.count(g.resolved(state.bits))) {
                if (!state.reset.empty()) throw std::runtime_error("streaming asynchronous reset is not supported");
                if (domainFound && (state.clock != clock || state.falling != falling))
                    throw std::runtime_error("streaming region must use one clock/edge");
                clock = state.clock; falling = state.falling;
                domainFound = true;
            }
            if (!domainFound) throw std::runtime_error("streaming region lost register ownership");
            g.states.erase(std::remove_if(g.states.begin(),g.states.end(),
                [&](const State& s) { return owned.count(g.resolved(s.bits)); }),g.states.end());
            Graph logic; logic.load(p.logic.c_str());
            std::set<size_t> expand;
            for (auto n : logic.dependencyOrder()) {
                const auto& cell = logic.nodes[n];
                // Place boundaries on the same gate network that will be
                // emitted. Word-level adder/multiplier estimates need not
                // match the ripple implementation used by the generic mapper.
                if (cell.op != "input" && cell.op != "state" && cell.op != "blackbox" &&
                    cell.op != "and" && cell.op != "or" && cell.op != "xor" && cell.op != "mux")
                    expand.insert(n);
            }
            if (!expand.empty()) logic = expandGates(std::move(logic),expand);
            const auto oldLatency = p.latency;
            const auto firstState = g.states.size();
            buildStreamPipeline(g,p,logic,[&](const Node& cell) { return cellDelay(logic,cell,model); },budget,model.clockToQ,inputArrival);
            for (size_t i = firstState; i < g.states.size(); ++i) {
                g.states[i].clock = clock; g.states[i].falling = falling;
            }
            for (const std::string name : {"result_out","fault_out","command_ready_out","response_valid_out"}) {
                const auto& before = oldPins.at(name);
                const auto& after = p.pins.at(name);
                for (size_t i = 0; i < before.size(); ++i) {
                    auto bit = g.resolve(before[i]), replacement = g.resolve(after[i]);
                    if (bit == replacement) continue;
                    if (bit < 2) throw std::runtime_error("streaming output changed a constant boundary");
                    g.connect({bit},{replacement});
                }
            }
            size_t newBits = 0; for (const auto& bits : p.registers) newBits += bits.size();
            report.insertedBits += newBits > oldBits ? newBits-oldBits : 0;
            report.addedLatency = std::max(report.addedLatency,p.latency-oldLatency);
        }
        auto timing = estimateTiming(g,model,rule.scope);
        report.after = timing.worst; report.met = report.after <= rule.period + 1e-9;
        if (!report.met) {
            std::string path;
            Value edge;
            for (const auto& state : g.states)
                if ("register " + g.nodes.at(Graph::owner(state.bits[0])).name == timing.endpoint) edge = state.next;
            for (unsigned depth = 0; depth < 64 && !edge.empty(); ++depth) {
                size_t owner = 0;
                double latest = 0;
                for (auto bit : g.resolved(edge)) if (bit > 1 && timing.arrival[Graph::owner(bit)] > latest) {
                    owner = Graph::owner(bit); latest = timing.arrival[owner];
                }
                if (!latest) break;
                const auto& node = g.nodes[owner];
                path += "\n  " + node.op + "[" + std::to_string(node.width) + "] " + node.name + " scope=" + node.scope +
                    " arrival=" + std::to_string(latest);
                if (node.op == "state" || node.op == "input") break;
                edge = node.left;
                edge.insert(edge.end(), node.right.begin(), node.right.end());
                edge.insert(edge.end(), node.select.begin(), node.select.end());
            }
            throw std::runtime_error("streaming target not met at boundary: " +
                timing.endpoint + " (" + std::to_string(report.after) + " ns)" + path);
        }
        g.validateClocks(); graph = std::move(g); return report;
    }
    for (const auto& attr : g.attributes) if (attr.name == "one_clock" && inside(attr.scope, rule.scope) &&
        std::stod(attr.value) + model.clockToQ + model.setup > rule.period + 1e-9)
        throw std::runtime_error("one-clock function exceeds target period: " + attr.scope);
    if (rule.mode == "keep_behaviour_retiming") {
        for (unsigned round = 0; round < 128; ++round) {
            auto bestDelay = estimateTiming(g, model, rule.scope).worst;
            if (bestDelay <= rule.period) break;
            std::optional<Graph> best;
            auto order = g.dependencyOrder();
            KeepBoxes boxes(g);
            for (bool forwards : {true, false}) {
                auto count = forwards ? order.size() : g.states.size();
                for (size_t i = 0; i < count; ++i) {
                    Graph candidate = g;
                    if (forwards && boxes.owner.count(order[i])) continue;
                    if (!forwards) {
                        bool boxed = false;
                        for (auto bit : g.resolved(g.states[i].next)) if (bit > 1) boxed |= boxes.owner.count(Graph::owner(bit)) != 0;
                        if (boxed) continue;
                    }
                    if (!(forwards ? forward(candidate, order[i], rule) : backward(candidate, i, rule))) continue;
                    auto delay = estimateTiming(candidate, model, rule.scope).worst;
                    if (delay + 1e-9 < bestDelay) { bestDelay = delay; best = std::move(candidate); }
                }
            }
            if (!best) break;
            g = std::move(*best); ++report.moved;
        }
    } else {
        // Word operators are not physical cells. Expose their gates when the
        // operation cannot fit between registers; keep explicit boxes intact.
        KeepBoxes boxes(g);
        std::set<size_t> expand;
        for (auto n : g.dependencyOrder()) {
            const auto& cell = g.nodes[n];
            if (!operation(cell) || !inside(cell.scope, rule.scope) || boxes.owner.count(n)) continue;
            if (cell.op == "and" || cell.op == "or" || cell.op == "xor" || cell.op == "mux" || cell.op == "memory_read") continue;
            if (cellDelay(g, cell, model) + model.clockToQ + model.setup + 2 * model.mux > rule.period + 1e-9)
                expand.insert(n);
        }
        if (!expand.empty()) g = expandGates(std::move(g), expand);
        // New boundary registers/reset muxes belong to the selected region,
        // never to the last module visited by a graph-building client.
        auto previousScope = g.currentScope;
        g.currentScope = rule.scope;
        Pipeline pipeline(g, rule, model, report.before <= rule.period + 1e-9); pipeline.run();
        g.currentScope = previousScope;
        report.insertedBits = pipeline.inserted; report.addedLatency = pipeline.latency;
        report.feedbackScheduled = pipeline.feedback;
        report.initiationInterval = pipeline.feedback ? pipeline.latency + 1 : 1;
    }
    auto finalTiming = estimateTiming(g, model, rule.scope);
    report.after = finalTiming.worst;
    report.met = report.after <= rule.period + 1e-9;
    if (rule.mode == "fit_pipeline_retiming" && !report.met)
        throw std::runtime_error("fit pipeline cannot meet target at an unchanged boundary: " + finalTiming.endpoint +
            " (" + std::to_string(report.after) + " ns, target " + std::to_string(rule.period) + " ns)");
    g.validateClocks(); graph = std::move(g);
    return report;
}
}
