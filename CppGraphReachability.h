#pragma once

#include "include/cpphdl_graph.h"
#include <unordered_map>

namespace cpphdl::cpp_graph {

// Final native models start with value-initialized state. Prove small closed
// state domains before removing undefined combinational fallbacks. This is not
// latch inference: reachable or observable missing writes remain diagnostics.
class NativeStateReachability {
    using Graph = graph::Graph;
    using Value = graph::Value;
    using Bit = graph::Bit;
    using Bool = unsigned;
    struct Limit {};
    struct Decision { Bit variable; Bool low, high; };
    class Bdd {
        std::vector<Decision> nodes{{UINT64_MAX, 0, 0}, {UINT64_MAX, 1, 1}};
        std::map<std::tuple<Bit, Bool, Bool>, Bool> unique;
        std::map<std::tuple<char, Bool, Bool>, Bool> memo;
        unsigned depth = 0;
        size_t* cost = nullptr;
        void budget() { if (cost && ++*cost > 2000000) throw Limit{}; }
    public:
        explicit Bdd(size_t* cost = nullptr) : cost(cost) {}
        Bool make(Bit variable, Bool low, Bool high) {
            budget();
            if (low == high) return low;
            auto key = std::make_tuple(variable, low, high);
            if (auto it = unique.find(key); it != unique.end()) return it->second;
            if (nodes.size() >= 50000) throw Limit{};
            Bool id = nodes.size(); nodes.push_back({variable, low, high}); unique.emplace(key, id); return id;
        }
        Bool variable(Bit bit) { return make(bit, 0, 1); }
        Bool apply(char op, Bool a, Bool b) {
            budget();
            if (a > b) std::swap(a, b);
            if (op == '&') { if (!a || a == b) return a; if (a == 1) return b; }
            if (op == '|') { if (!a || a == b) return b; if (a == 1) return 1; }
            if (op == '^') { if (!a) return b; if (a == b) return 0; }
            if (a < 2 && b < 2) return op == '&' ? a & b : op == '|' ? a | b : a ^ b;
            auto key = std::make_tuple(op, a, b);
            if (auto it = memo.find(key); it != memo.end()) return it->second;
            if (++depth > 512) throw Limit{};
            auto first = std::min(nodes[a].variable, nodes[b].variable);
            auto left = nodes[a], right = nodes[b];
            auto low = apply(op, left.variable == first ? left.low : a, right.variable == first ? right.low : b);
            auto high = apply(op, left.variable == first ? left.high : a, right.variable == first ? right.high : b);
            auto result = make(first, low, high);
            --depth; memo.emplace(key, result); return result;
        }
        Bool choose(Bool select, Bool yes, Bool no) {
            if (select < 2) return select ? yes : no;
            if (yes == no) return yes;
            return apply('|', apply('&', select, yes), apply('&', apply('^', select, 1), no));
        }
        bool depends(Bool value, size_t wire) {
            std::vector<Bool> pending{value}; std::set<Bool> seen;
            while (!pending.empty()) {
                budget();
                auto id = pending.back(); pending.pop_back();
                if (id < 2 || !seen.insert(id).second) continue;
                const auto& n = nodes[id];
                if (Graph::owner(n.variable) == wire) return true;
                pending.push_back(n.low); pending.push_back(n.high);
            }
            return false;
        }
    } bdd;
    Graph& g;
    size_t target = 0, visits = 0, totalVisits = 0;
    std::map<Bit, Bool> fixed;
    std::unordered_map<size_t, std::vector<Bool>> cache;
    std::set<size_t> active;
    void budget() {
        if (++visits > 250000 || ++totalVisits > 2000000 || active.size() >= 512) throw Limit{};
    }
    std::vector<Bool> bits(const Value& value) {
        budget(); std::vector<Bool> result; result.reserve(value.size());
        for (auto raw : value) {
            auto bit = g.resolve(raw);
            if (bit < 2) result.push_back(bit);
            else if (auto it = fixed.find(bit); it != fixed.end()) result.push_back(it->second);
            else result.push_back(node(Graph::owner(bit))[Graph::lane(bit)]);
        }
        return result;
    }
    static std::optional<uint64_t> number(const std::vector<Bool>& value) {
        if (value.size() > 64) return {};
        uint64_t result = 0;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] > 1) return {};
            result |= uint64_t(value[i]) << i;
        }
        return result;
    }
    bool poisoned(const std::vector<Bool>& value) {
        for (auto bit : value) if (bdd.depends(bit, target)) return true;
        return false;
    }
    const std::vector<Bool>& node(size_t id) {
        if (auto it = cache.find(id); it != cache.end()) return it->second;
        budget(); if (!active.insert(id).second) throw Limit{};
        const auto& n = g.nodes[id];
        std::vector<Bool> result;
        if (n.op == "wire" || n.op == "state" || n.op == "input") {
            for (unsigned i = 0; i < n.width; ++i) result.push_back(bdd.variable((id + 1) * 64 + 2 + i));
        } else if (n.op == "mux") {
            auto condition = bits(n.select).at(0);
            if (condition < 2) result = bits(condition ? n.left : n.right);
            else {
                auto yes = bits(n.left), no = bits(n.right);
                for (unsigned i = 0; i < n.width; ++i) result.push_back(bdd.choose(condition, yes.at(i), no.at(i)));
            }
        } else {
            auto a = bits(n.left), b = bits(n.right), select = bits(n.select);
            if (n.op == "and" || n.op == "or" || n.op == "xor") {
                for (unsigned i = 0; i < n.width; ++i)
                    result.push_back(bdd.apply(n.op == "and" ? '&' : n.op == "or" ? '|' : '^', a.at(i), b.at(i)));
            } else if (n.op == "any" || n.op == "all" || n.op == "parity") {
                Bool reduced = n.op == "all";
                for (auto bit : a) reduced = bdd.apply(n.op == "all" ? '&' : n.op == "any" ? '|' : '^', reduced, bit);
                result.push_back(reduced);
            } else {
                static const std::set<std::string> arithmetic{
                    "add", "sub", "mul", "div", "mod", "eq", "lt", "shl", "shr"};
                auto lhs = number(a), rhs = number(b);
                if (arithmetic.count(n.op) && lhs && rhs) {
                    auto value = Graph::calculate(n.op, *lhs, *rhs, n.width);
                    for (unsigned i = 0; i < n.width; ++i) result.push_back((value >> i) & 1);
                } else {
                    // Opaque operations may correlate with their inputs. Treat
                    // their outputs as arbitrary, stable symbols only if none
                    // of their arguments can depend on the missing value.
                    if (poisoned(a) || poisoned(b) || poisoned(select)) throw Limit{};
                    for (unsigned i = 0; i < n.width; ++i) result.push_back(bdd.variable((id + 1) * 64 + 2 + i));
                }
            }
        }
        active.erase(id); return cache.emplace(id, std::move(result)).first->second;
    }
    void condition(const graph::State& state, uint64_t value) {
        fixed.clear(); cache.clear(); active.clear(); bdd = Bdd{&totalVisits};
        for (size_t i = 0; i < state.bits.size(); ++i) {
            auto bit = g.resolve(state.bits[i]);
            if (bit < 2 || g.nodes[Graph::owner(bit)].op != "state") throw Limit{};
            if (!fixed.emplace(bit, (value >> i) & 1).second) throw Limit{};
        }
    }
    struct Root { Value bits; std::optional<size_t> state; };
    bool prove(size_t stateId, const std::vector<Root>& roots, const std::set<size_t>& affectedRoots) {
        const auto& state = g.states[stateId];
        if (state.bits.empty() || state.bits.size() > 6 || state.next.size() != state.bits.size()) return false;
        visits = 0;
        try {
            // This is native-only: generated state banks are value initialized.
            // Include every asynchronous reset value in the inductive base too.
            std::set<uint64_t> reachable{0}, evaluated;
            if (!state.resetValue.empty()) {
                auto reset = graph::number(g.resolved(state.resetValue));
                if (!reset) return false;
                reachable.insert(*reset);
            }
            while (evaluated.size() != reachable.size()) {
                auto current = *std::find_if(reachable.begin(), reachable.end(), [&](auto v) { return !evaluated.count(v); });
                condition(state, current);
                auto next = bits(state.next);
                if (poisoned(next)) return false;
                // Preserve correlations between individual bit aliases: {3,4}
                // is a two-element set, not the per-bit approximation 0..7.
                for (uint64_t value = 0; value <= graph::mask(next.size()); ++value) {
                    Bool match = 1;
                    for (size_t i = 0; i < next.size(); ++i)
                        match = bdd.apply('&', match, ((value >> i) & 1) ? next[i] : bdd.apply('^', next[i], 1));
                    if (match) reachable.insert(value);
                }
                evaluated.insert(current);
            }
            // A closed transition set alone is insufficient: another output,
            // memory access or host effect could expose the missing value.
            for (auto current : reachable) {
                condition(state, current);
                for (auto root : affectedRoots) if (poisoned(bits(roots[root].bits))) return false;
            }
            return true;
        } catch (const Limit&) { return false; }
    }
public:
    explicit NativeStateReachability(Graph& graph) : g(graph) {}
    size_t run() {
        if (!g.pipelines.empty()) return 0;
        std::vector<Root> roots;
        auto root = [&](const Value& bits, std::optional<size_t> state = {}) {
            if (!bits.empty()) roots.push_back({bits, state});
        };
        for (const auto& port : g.ports) if (!port.input) root(port.bits);
        for (size_t i = 0; i < g.states.size(); ++i) {
            const auto& s = g.states[i]; root(s.next, i); root(s.trigger, i); root(s.reset, i); root(s.resetValue, i);
        }
        for (const auto& a : g.memoryAccesses) { root(a.address); root(a.enabled); }
        for (const auto& w : g.memoryWrites) { root(w.address); root(w.data); root(w.enabled); }
        for (size_t i = 0; i < g.nodes.size(); ++i)
            if (g.nodes[i].hostEffect() || g.nodes[i].validation()) root(Value{(i + 1) * 64 + 2});
        std::vector<std::vector<size_t>> parents(g.nodes.size()), users(g.nodes.size());
        std::set<size_t> missing;
        std::vector<bool> visited(g.nodes.size());
        std::vector<size_t> pending;
        auto owners = [&](const Value& value) {
            std::set<size_t> result;
            for (auto bit : value) if ((bit = g.resolve(bit)) > 1) result.insert(Graph::owner(bit));
            return result;
        };
        for (size_t i = 0; i < roots.size(); ++i) for (auto id : owners(roots[i].bits)) {
            users[id].push_back(i); pending.push_back(id);
        }
        while (!pending.empty()) {
            auto id = pending.back(); pending.pop_back();
            if (visited[id]) continue;
            visited[id] = true;
            const auto& n = g.nodes[id];
            if (n.op == "wire") missing.insert(id);
            for (const auto* operand : {&n.left, &n.right, &n.select}) for (auto child : owners(*operand)) {
                parents[child].push_back(id); pending.push_back(child);
            }
        }
        size_t removed = 0;
        for (auto wire : missing) {
            target = wire;
            std::set<size_t> affectedRoots, candidates, seen;
            pending = {wire};
            while (!pending.empty()) {
                auto id = pending.back(); pending.pop_back();
                if (!seen.insert(id).second) continue;
                for (auto user : users[id]) {
                    affectedRoots.insert(user);
                    if (roots[user].state) candidates.insert(*roots[user].state);
                }
                pending.insert(pending.end(), parents[id].begin(), parents[id].end());
            }
            unsigned attempts = 0;
            for (auto state : candidates) {
                if (g.states[state].bits.size() > 6) continue;
                if (++attempts > 16 || totalVisits > 2000000) break;
                if (!prove(state, roots, affectedRoots)) continue;
                for (unsigned bit = 0; bit < g.nodes[wire].width; ++bit) {
                    Bit original = (wire + 1) * 64 + 2 + bit;
                    if (g.resolve(original) == original) g.aliases[original] = 0;
                }
                ++removed;
                break;
            }
        }
        return removed;
    }
};

} // namespace cpphdl::cpp_graph
