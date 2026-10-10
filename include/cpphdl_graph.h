#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CPPHDL_NATIVE_THREADS
#define CPPHDL_NATIVE_THREADS 1
#endif

namespace cpphdl::graph {

using Bit = uint64_t;
using Value = std::vector<Bit>;

// Bits are references, not stored simulation objects. Concatenation, fields,
// ports and ordered partial writes therefore cost nothing at runtime.
struct Node {
    std::string op;
    unsigned width = 0;
    Value left, right, select;
    std::string name;
    std::string scope;
    bool hostEffect() const { return op == "host_random" || op == "host_jtag_tick" || op == "host_debug_tick"; }
    bool validation() const { return op == "assert_failure"; }
};
struct Port { std::string name; Value bits; bool input; };
struct State { Value bits, next, trigger; int clock = -1; bool falling = false; Value reset, resetValue; };
struct Clock { std::string name; uint64_t frequency; };
struct Memory {
    std::string name; unsigned width; uint64_t depth;
    // Nonempty contents describe an immutable ROM, row-major with each row
    // rounded up to 64-bit words. Mutable RAM retains its existing semantics.
    std::vector<uint64_t> contents;
};
struct MemoryAccess { size_t memory; Value address, enabled; bool transaction; int clock = -1; bool falling = false; };
struct MemoryWrite { size_t memory; Value address, data, enabled; int clock = -1; bool falling = false; };
struct ScopeAttribute { std::string scope, name, value; };

// A streaming region retains its untimed transition graph for latency-changing
// scheduling. Boundary bindings refer to this graph; logic uses local bit IDs.
struct StreamPipeline {
    std::string scope, logic;
    std::map<std::string, Value> pins;
    std::vector<Value> registers;
    unsigned stages = 1, latency = 1, generation = 0;
};

// RisingEdgeStep means one lifecycle transaction per rising edge. Reset and
// enable are next-state logic; ExplicitEvents preserves frontend event logic.
enum class ClockContract { ExplicitEvents, RisingEdgeStep, NamedEdges };

inline uint64_t mask(unsigned width) {
    return width >= 64 ? ~uint64_t(0) : (uint64_t(1) << width) - 1;
}
inline Value constant(uint64_t number, unsigned width) {
    Value result(width);
    for (unsigned index = 0; index < width; ++index)
        result[index] = index < 64 ? (number >> index) & 1 : 0;
    return result;
}
inline std::optional<uint64_t> number(const Value& value) {
    if (value.size() > 64) return {};
    uint64_t result = 0;
    for (unsigned index = 0; index < value.size(); ++index) {
        if (value[index] > 1) return {};
        result |= value[index] << index;
    }
    return result;
}
inline Value resize(Value value, unsigned width, bool sign = false) {
    value.resize(width, sign && !value.empty() ? value.back() : 0);
    return value;
}
inline Value slice(const Value& value, int64_t offset, unsigned width) {
    Value result(width);
    for (unsigned index = 0; index < width; ++index)
        if (offset + index >= 0 && uint64_t(offset + index) < value.size())
            result[index] = value[offset + index];
    return result;
}

class Graph {
public:
    ClockContract clockContract = ClockContract::ExplicitEvents;
    std::vector<Clock> clocks;
    // Node IDs are indices. Segmented storage keeps them stable without a
    // second, doubled allocation when a multi-million-node graph grows.
    std::deque<Node> nodes;
    std::map<Bit, Bit> aliases;
    std::vector<Port> ports;
    std::vector<State> states;
    std::vector<Memory> memories;
    std::vector<MemoryAccess> memoryAccesses;
    std::vector<MemoryWrite> memoryWrites;
    std::string currentScope;
    std::vector<ScopeAttribute> attributes;
    std::vector<StreamPipeline> pipelines;

    void validateClocks() const {
        for (const auto& state : states) if (!state.reset.empty()) {
            if (state.reset.size() != 1 || state.resetValue.size() != state.bits.size() ||
                std::any_of(state.resetValue.begin(), state.resetValue.end(), [](Bit bit) { return bit > 1; }))
                throw std::runtime_error("asynchronous reset requires a constant full-width value");
            bool input = false;
            for (const auto& port : ports) input |= port.input && port.bits == state.reset;
            if (!input) throw std::runtime_error("asynchronous reset must be a scalar input port");
        }
        std::map<size_t, std::pair<int, bool>> writers;
        auto checkMemoryClock = [&](int clock, bool falling) {
            if (clockContract == ClockContract::NamedEdges ? (clock < 0 || size_t(clock) >= clocks.size()) : (clock != -1 || falling))
                throw std::runtime_error("invalid memory clock domain");
        };
        for (const auto& memory : memories) {
            if (!memory.width || memory.width > 1048576 || !memory.depth)
                throw std::runtime_error("invalid graph memory dimensions");
            if (!memory.contents.empty()) {
                const auto words = (memory.width + 63) / 64;
                if (memory.contents.size() % words || memory.contents.size() / words != memory.depth)
                    throw std::runtime_error("invalid graph ROM contents");
                for (size_t i = words - 1; i < memory.contents.size(); i += words)
                    if (memory.contents[i] & ~mask((memory.width - 1) % 64 + 1))
                        throw std::runtime_error("nonzero graph ROM padding");
            }
        }
        for (const auto& access : memoryAccesses) {
            if (access.memory >= memories.size() || access.address.empty() || access.address.size() > 64 || access.enabled.size() != 1)
                throw std::runtime_error("invalid graph memory access");
            if (access.transaction) checkMemoryClock(access.clock, access.falling);
            else if (access.clock != -1 || access.falling) throw std::runtime_error("combinational memory access has a clock");
        }
        for (const auto& write : memoryWrites) {
            if (write.memory >= memories.size() || write.data.size() != memories[write.memory].width ||
                write.address.empty() || write.address.size() > 64 || write.enabled.size() != 1)
                throw std::runtime_error("invalid graph memory write");
            if (!memories[write.memory].contents.empty()) throw std::runtime_error("write to graph ROM");
            checkMemoryClock(write.clock, write.falling);
            auto domain = std::make_pair(write.clock, write.falling);
            auto [entry, fresh] = writers.emplace(write.memory, domain);
            if (!fresh && entry->second != domain) throw std::runtime_error("multiple clock/edge writers for memory");
        }
        if (clockContract != ClockContract::NamedEdges) {
            if (!clocks.empty()) throw std::runtime_error("clock domains require a named-edge contract");
            for (const auto& state : states)
                if (state.clock != -1 || state.falling) throw std::runtime_error("unexpected state clock domain");
            return;
        }
        if (clocks.empty()) throw std::runtime_error("named-edge graph has no clocks");
        std::set<std::string> names;
        for (const auto& clock : clocks) {
            if (clock.name.empty() || !clock.frequency || !names.insert(clock.name).second ||
                (!std::isalpha(static_cast<unsigned char>(clock.name[0])) && clock.name[0] != '_'))
                throw std::runtime_error("invalid or duplicate clock name/frequency");
            for (unsigned char c : clock.name) if (!std::isalnum(c) && c != '_')
                throw std::runtime_error("invalid clock identifier");
            if (clock.name == "eval" || clock.name == "step" || clock.name == "evaluate" ||
                clock.name.find("__cpphdl_") == 0)
                throw std::runtime_error("reserved clock name: " + clock.name);
            for (const auto& port : ports) if (port.name == clock.name)
                throw std::runtime_error("clock conflicts with data port: " + clock.name);
        }
        std::set<Bit> owners;
        for (const auto& state : states) {
            if (state.clock < 0 || size_t(state.clock) >= clocks.size() || state.trigger != Value{1})
                throw std::runtime_error("invalid state clock domain/trigger");
            for (auto bit : state.bits) if (!owners.insert(bit).second)
                throw std::runtime_error("multiple clock/edge owners for state");
        }
    }

    Value add(std::string op, unsigned width, Value left = {}, Value right = {},
              Value select = {}, std::string name = {}) {
        if (!width || width > 64) throw std::runtime_error("invalid graph node width");
        auto base = (nodes.size() + 1) * 64 + 2;
        Node node{std::move(op), width, std::move(left), std::move(right),
                  std::move(select), std::move(name), currentScope};
        Value result(width);
        for (unsigned index = 0; index < width; ++index) result[index] = base + index;
        // Resolve identities before allocating persistent nodes or aliases.
        // The same rules still run after hierarchy connections become known.
        simplifyNode(node, [&](unsigned offset, Bit bit) { result[offset] = bit; });
        if (std::none_of(result.begin(), result.end(),
                         [&](Bit bit) { return bit >= base && bit < base + width; }))
            return result;
        nodes.push_back(std::move(node));
        for (unsigned index = 0; index < width; ++index)
            if (result[index] != base + index) aliases[base + index] = result[index];
        return result;
    }
    static size_t owner(Bit bit) { return (bit - 2) / 64 - 1; }
    static unsigned lane(Bit bit) { return (bit - 2) % 64; }
    Value wire(unsigned width, std::string name, std::string op = "wire") {
        Value result;
        for (unsigned offset = 0; offset < width; offset += 64) {
            auto part = add(op, std::min(64u, width - offset), {}, {}, {},
                            name + ":" + std::to_string(offset));
            result.insert(result.end(), part.begin(), part.end());
        }
        return result;
    }
    Bit resolve(Bit bit) {
        std::vector<Bit> path;
        auto cursor = bit;
        while (aliases.count(cursor)) {
            if (std::find(path.begin(), path.end(), cursor) != path.end())
                throw std::runtime_error("cyclic wire alias");
            path.push_back(cursor);
            cursor = aliases.at(cursor);
        }
        for (auto entry : path) aliases[entry] = cursor;
        return cursor;
    }
    Value resolved(Value value) {
        for (auto& bit : value) bit = resolve(bit);
        return value;
    }
    void connect(const Value& target, const Value& source) {
        if (target.size() != source.size()) throw std::runtime_error("connection width mismatch");
        for (size_t index = 0; index < target.size(); ++index) {
            if (target[index] < 2 || aliases.count(target[index]))
                throw std::runtime_error("multiple drivers in native graph: " + (target[index] < 2 ? std::string("constant") : nodes[owner(target[index])].name) + " bit " + std::to_string(lane(target[index])));
            if (target[index] != source[index]) aliases[target[index]] = source[index];
        }
    }
    Value unary(std::string op, Value value) {
        if (op == "not") return binary("xor", value, Value(value.size(), 1), value.size());
        Value result{op == "all" ? Bit(1) : Bit(0)};
        for (unsigned offset = 0; offset < value.size(); offset += 64) {
            auto part = slice(value, offset, std::min<size_t>(64, value.size() - offset));
            Value reduced;
            if (auto known = number(part)) {
                bool flag = op == "all" ? *known == mask(part.size()) :
                            op == "parity" ? __builtin_parityll(*known) : *known != 0;
                reduced = constant(flag, 1);
            } else reduced = add(op, 1, part);
            result = binary(op == "all" ? "and" : op == "parity" ? "xor" : "or",
                            result, reduced, 1);
        }
        return result;
    }
    Value constantArray(Value data, Value address, unsigned rowWidth) {
        data = resolved(std::move(data));
        if (!rowWidth || data.empty() || data.size() % rowWidth || address.empty() || address.size() > 64 ||
            std::any_of(data.begin(), data.end(), [](Bit bit) { return bit > 1; }))
            throw std::runtime_error("invalid constant array lookup");
        const auto depth = data.size() / rowWidth;
        if (auto known = number(resolved(address)))
            return *known < depth ? slice(data, *known * rowWidth, rowWidth) : Value(rowWidth, 0);
        const auto words = (rowWidth + 63) / 64;
        std::vector<uint64_t> contents(depth * words, 0);
        for (size_t row = 0; row < depth; ++row)
            for (unsigned bit = 0; bit < rowWidth; ++bit)
                contents[row * words + bit / 64] |= data[row * rowWidth + bit] << (bit % 64);
        size_t identity = 0;
        for (; identity < memories.size(); ++identity)
            if (memories[identity].width == rowWidth && memories[identity].depth == depth &&
                memories[identity].contents == contents) break;
        if (identity == memories.size())
            memories.push_back({currentScope + "/rom" + std::to_string(identity), rowWidth, depth, std::move(contents)});
        Value result;
        for (unsigned offset = 0; offset < rowWidth; offset += 64) {
            auto part = add("memory_read", std::min(64u, rowWidth - offset), address,
                            constant(identity, 64), constant(offset / 64, 64));
            result.insert(result.end(), part.begin(), part.end());
        }
        return result;
    }
    Value binary(std::string op, Value left, Value right, unsigned width) {
        if (op == "mul") {
            // Constant power-of-two multiplication is wiring, not an adder
            // tree. Canonicalize before timing as well as gate mapping.
            auto power = [&](Value value) {
                int shift = -1;
                value = resolved(value);
                for (size_t i = 0; i < value.size(); ++i) {
                    if (value[i] > 1) return -1;
                    if (value[i]) {
                        if (shift >= 0) return -1;
                        shift = int(i);
                    }
                }
                return shift;
            };
            int shift = power(right);
            if (shift < 0) { shift = power(left); if (shift >= 0) std::swap(left, right); }
            if (shift >= 0) {
                Value result(std::min<unsigned>(shift, width), 0);
                result.insert(result.end(), left.begin(), left.end());
                return resize(result, width);
            }
        }
        if (op == "and" || op == "or" || op == "xor" || op == "mux") {
            left = resize(left, width); right = resize(right, width);
            Value result;
            for (unsigned offset = 0; offset < width;) {
                unsigned count = std::min(64u, width - offset);
                auto part = add(op, count, slice(left, offset, count), slice(right, offset, count));
                result.insert(result.end(), part.begin(), part.end());
                offset += count;
            }
            return result;
        }
        if (left.size() > 64 || right.size() > 64 || width > 64) {
            if (op == "eq") {
                auto count = std::max(left.size(), right.size());
                auto different = binary("xor", resize(left, count), resize(right, count), count);
                return resize(unary("not", unary("any", different)), width);
            }
            if (op == "lt") {
                auto count = std::max(left.size(), right.size());
                left = resize(left, count); right = resize(right, count);
                Value less{0};
                for (size_t offset = 0; offset < count; offset += 64) {
                    auto length = std::min<size_t>(64, count - offset);
                    auto a = slice(left, offset, length), b = slice(right, offset, length);
                    less = mux(binary("eq", a, b, 1), less, binary("lt", a, b, 1));
                }
                return resize(less, width);
            }
            if (op == "shr" || op == "sar") {
                const Bit fill = op == "sar" && !left.empty() ? left.back() : 0;
                left.resize(std::max<size_t>(left.size(), width), fill);
                unsigned stages = 0;
                while ((uint64_t(1) << stages) < left.size()) ++stages;
                // Visit large shifts first and discard bits that no remaining
                // smaller shift can move into the result. A ROM bit lookup
                // must not construct a full-width shifter at every stage.
                for (unsigned stage = stages; stage-- > 0;) {
                    const size_t amount = uint64_t(1) << stage;
                    const size_t needed = std::min(left.size(), size_t(width) + amount - 1);
                    Value moved(needed, fill);
                    for (size_t i = 0; i < needed; ++i)
                        if (amount < left.size() - i) moved[i] = left[i + amount];
                    left.resize(needed, fill);
                    if (stage < right.size()) left = mux({right[stage]}, moved, left);
                }
                left.resize(width, fill);
                if (right.size() > stages)
                    left = mux(unary("any", slice(right, stages, right.size() - stages)), Value(width, fill), left);
                return left;
            }
            if (op == "shl") {
                const Bit fill = op == "sar" && !left.empty() ? left.back() : 0;
                // Keep the source width until after a right shift: narrowing
                // the result must not discard source bits that shift into it.
                left.resize(std::max<size_t>(left.size(), width), fill);
                for (unsigned stage = 0; stage < right.size(); ++stage) {
                    Value moved(left.size(), fill);
                    if (stage < 63) {
                        uint64_t amount = uint64_t(1) << stage;
                        for (size_t i = 0; i < left.size(); ++i)
                            if (op == "shl" ? amount <= i : amount < left.size() - i)
                                moved[i] = left[op == "shl" ? i - amount : i + amount];
                    }
                    left = mux({right[stage]}, moved, left);
                }
                return resize(left, width);
            }
            // Arithmetic limbs keep every node evaluable by the native 64-bit
            // executor while preserving carries above bit 63 for synthesis.
            if (op == "add" || op == "sub") {
                left = resize(left, width); right = resize(right, width);
                Value result, carry{Bit(op == "sub")};
                for (unsigned offset = 0; offset < width; offset += 32) {
                    unsigned count = std::min(32u, width - offset);
                    auto a = resize(slice(left, offset, count), count + 1);
                    auto b = slice(right, offset, count);
                    if (op == "sub") b = unary("not", b);
                    auto sum = binary("add", binary("add", a, resize(b, count + 1), count + 1),
                                      resize(carry, count + 1), count + 1);
                    auto part = slice(sum, 0, count);
                    result.insert(result.end(), part.begin(), part.end());
                    carry = slice(sum, count, 1);
                }
                return result;
            }
            if (op == "mul") {
                left = resize(left, width); right = resize(right, width);
                Value result(width, 0);
                for (unsigned a = 0; a < width; a += 32)
                    for (unsigned b = 0; a + b < width; b += 32) {
                        auto lhs = slice(left, a, std::min(32u, width - a));
                        auto rhs = slice(right, b, std::min(32u, width - b));
                        auto product = binary("mul", resize(lhs, 64), resize(rhs, 64), 64);
                        Value row(a + b, 0);
                        row.insert(row.end(), product.begin(), product.end());
                        result = binary("add", result, resize(row, width), width);
                    }
                return result;
            }
            throw std::runtime_error("wide arithmetic not supported: " + op);
        }
        if (auto lhs = number(left)) if (auto rhs = number(right)) {
            auto result = calculate(op, *lhs, *rhs, left.size());
            return constant(result, width);
        }
        return add(op, width, left, right);
    }
    Value mux(Value condition, Value yes, Value no) {
        condition = unary("any", condition);
        if (yes.size() != no.size()) throw std::runtime_error("mux width mismatch");
        if (condition[0] < 2) return condition[0] ? yes : no;
        if (yes == no) return yes;
        Value result;
        for (unsigned offset = 0; offset < yes.size(); offset += 64) {
            auto count = std::min<size_t>(64, yes.size() - offset);
            auto part = add("mux", count, slice(yes, offset, count), slice(no, offset, count), condition);
            result.insert(result.end(), part.begin(), part.end());
        }
        return result;
    }
    static int64_t signedNumber(uint64_t value, unsigned width) {
        if (width == 64) return int64_t(value);
        auto sign = uint64_t(1) << (width - 1);
        return int64_t((value ^ sign) - sign);
    }
    static uint64_t calculate(const std::string& op, uint64_t left, uint64_t right, unsigned width) {
        if (op == "add") return left + right;
        if (op == "sub") return left - right;
        if (op == "mul") return left * right;
        if (op == "div") return right ? left / right : 0;
        if (op == "mod") return right ? left % right : 0;
        if (op == "sdiv" || op == "smod") {
            auto lhs = signedNumber(left, width), rhs = signedNumber(right, width);
            if (!rhs) return 0;
            // Fixed-width graph arithmetic wraps, including MIN / -1.
            if (lhs == INT64_MIN && rhs == -1) return op == "sdiv" ? left : 0;
            return uint64_t(op == "sdiv" ? lhs / rhs : lhs % rhs);
        }
        if (op == "eq") return left == right;
        if (op == "lt") return left < right;
        if (op == "slt") return signedNumber(left, width) < signedNumber(right, width);
        if (op == "shl") return right < 64 ? left << right : 0;
        if (op == "shr") return right < 64 ? left >> right : 0;
        if (op == "sar") return uint64_t(signedNumber(left, width) >> std::min<uint64_t>(right, 63));
        throw std::runtime_error("unsupported graph operation: " + op);
    }

    // Synthesis retiming can connect aliases after construction. Revisit the
    // affected producers without losing the eager folds used by simulation.
    void simplifyLast(const Value& value) {
        std::set<size_t> owners;
        for (auto bit : value) if (bit > 1) owners.insert(owner(bit));
        for (auto index : owners) simplify(index);
    }
    bool simplify(size_t index) {
        bool changed = false;
        simplifyNode(nodes[index], [&](unsigned offset, Bit bit) {
            Bit target = (index + 1) * 64 + 2 + offset;
            if (!aliases.count(target) && target != bit) { aliases[target] = bit; changed = true; }
        });
        return changed;
    }
    template<class Redirect> void simplifyNode(Node& node, Redirect redirect) {
        node.left = resolved(std::move(node.left)); node.right = resolved(std::move(node.right));
        node.select = resolved(std::move(node.select));
        if (node.hostEffect() || node.validation() || node.op.compare(0, 3, "fp_") == 0 || node.op == "memory_read" || node.op == "blackbox") return;
        if (node.op == "mux" || node.op == "and" || node.op == "or" || node.op == "xor") {
            for (unsigned offset = 0; offset < node.width; ++offset) {
                auto left = node.left[offset], right = node.right[offset];
                if (node.op == "mux") {
                    if (node.select[0] < 2) redirect(offset, node.select[0] ? left : right);
                    else if (left == right) redirect(offset, left);
                    else if (left == 1 && right == 0) redirect(offset, node.select[0]);
                } else if (node.op == "and") {
                    if (!left || !right) redirect(offset, 0);
                    else if (left == 1 || left == right) redirect(offset, right);
                    else if (right == 1) redirect(offset, left);
                } else if (node.op == "or") {
                    if (left == 1 || right == 1) redirect(offset, 1);
                    else if (!left || left == right) redirect(offset, right);
                    else if (!right) redirect(offset, left);
                } else {
                    if (left == right) redirect(offset, 0);
                    else if (!left) redirect(offset, right);
                    else if (!right) redirect(offset, left);
                }
            }
        } else if (node.op == "any" || node.op == "all" || node.op == "parity") {
            if (auto known = number(node.left))
                redirect(0, node.op == "any" ? *known != 0 : node.op == "all" ?
                         *known == mask(node.left.size()) : __builtin_parityll(*known));
            else if (node.op != "parity") {
                auto dominant = node.op == "any" ? Bit(1) : Bit(0);
                if (std::find(node.left.begin(), node.left.end(), dominant) != node.left.end())
                    redirect(0, dominant);
                else {
                    std::set<Bit> unique(node.left.begin(), node.left.end());
                    unique.erase(1 - dominant);
                    if (unique.size() == 1) redirect(0, *unique.begin());
                }
            }
        } else if (node.op == "shl" || node.op == "shr" || node.op == "sar") {
            // C++ bit selections often arrive as a word shift followed by a
            // mask. Constant shifts are wiring: retaining a whole-word input
            // falsely reads unwritten sibling bits in a procedural bit tree.
            if (auto shift = number(node.right)) {
                for (unsigned offset = 0; offset < node.width; ++offset) {
                    Bit bit = node.op == "sar" ? node.left.back() : 0;
                    if (node.op == "shl") {
                        if (*shift <= offset && offset - *shift < node.left.size()) bit = node.left[offset - *shift];
                    } else if (*shift < node.left.size() && offset < node.left.size() - *shift) bit = node.left[offset + *shift];
                    redirect(offset, bit);
                }
            }
        } else if (!node.right.empty()) {
            if (auto left = number(node.left)) if (auto right = number(node.right)) {
                auto result = constant(calculate(node.op, *left, *right, node.left.size()), node.width);
                for (unsigned offset = 0; offset < node.width; ++offset) redirect(offset, result[offset]);
            }
            if (node.op == "eq" && node.left == node.right) redirect(0, 1);
        }
    }

    void optimize() {
        for (unsigned round = 0; round < 100; ++round) {
            bool changed = false;
            for (size_t index = 0; index < nodes.size(); ++index) changed |= simplify(index);
            if (!changed) break;
            if (round == 99) throw std::runtime_error("graph simplification did not converge");
        }
        // Share producers after resolving hierarchy aliases, not before their
        // actual connectivity is known. State/input nodes and host effects
        // are never merged, even when their arguments are identical.
        std::map<std::string, size_t> shared;
        for (size_t index = 0; index < nodes.size(); ++index) {
            auto& node = nodes[index];
            if (node.op == "wire" || node.op == "input" || node.op == "state" || node.hostEffect() || node.validation()) continue;
            node.left = resolved(node.left); node.right = resolved(node.right); node.select = resolved(node.select);
            std::ostringstream key;
            key << node.scope << ':' << node.op << ':' << node.width;
            if (node.op == "blackbox") key << ':' << node.name;
            for (auto* value : {&node.left, &node.right, &node.select}) {
                key << '/'; for (auto bit : *value) key << bit << ',';
            }
            auto [entry, fresh] = shared.emplace(key.str(), index);
            if (!fresh) for (unsigned offset = 0; offset < node.width; ++offset) {
                auto bit = (index + 1) * 64 + 2 + offset;
                if (!aliases.count(bit)) aliases[bit] = (entry->second + 1) * 64 + 2 + offset;
            }
        }
    }

    // Final native emission can discard state outside the observable sequential
    // cone. Do not run this during partition extraction or streaming scheduling:
    // those graphs can still acquire consumers/bindings at a later stage.
    size_t pruneUnusedState() {
        if (states.empty() || !pipelines.empty()) return 0;
        std::vector<std::vector<size_t>> owners(nodes.size());
        for (size_t index = 0; index < states.size(); ++index) {
            std::set<size_t> words;
            for (auto bit : states[index].bits) if (bit > 1) words.insert(owner(bit));
            for (auto word : words) owners[word].push_back(index);
        }
        std::vector<bool> live(nodes.size()), stateLive(states.size());
        std::vector<size_t> pending;
        auto requireNode = [&](size_t index) {
            if (!live[index]) { live[index] = true; pending.push_back(index); }
        };
        auto require = [&](const Value& value) {
            for (auto raw : value) {
                auto bit = resolve(raw);
                if (bit > 1) requireNode(owner(bit));
            }
        };
        for (const auto& port : ports) if (!port.input) require(port.bits);
        // Even an unread memory can have writes or observable bounds errors.
        for (const auto& access : memoryAccesses) {
            require(access.address); require(access.enabled);
        }
        for (const auto& write : memoryWrites) {
            require(write.address); require(write.data); require(write.enabled);
        }
        for (size_t index = 0; index < nodes.size(); ++index)
            if (nodes[index].hostEffect() || nodes[index].validation()) requireNode(index);
        while (!pending.empty()) {
            auto index = pending.back(); pending.pop_back();
            const auto& node = nodes[index];
            if (node.op == "state") {
                // Retain the whole update group when any word is observable.
                // Following next-state dependencies also preserves values that
                // become observable only on a later cycle.
                for (auto stateIndex : owners[index]) if (!stateLive[stateIndex]) {
                    stateLive[stateIndex] = true;
                    const auto& state = states[stateIndex];
                    require(state.next); require(state.trigger);
                    require(state.reset); require(state.resetValue);
                }
            } else if (node.op != "input") {
                require(node.left); require(node.right); require(node.select);
            }
        }
        size_t retained = 0;
        for (size_t index = 0; index < states.size(); ++index) if (stateLive[index]) {
            if (retained != index) states[retained] = std::move(states[index]);
            ++retained;
        }
        auto removed = states.size() - retained;
        states.resize(retained);
        return removed;
    }

    // Partition files retain only live, resolved values. In particular, dead
    // locals and memoization aliases must not accumulate across instances.
    void compact() {
        if (!pipelines.empty()) throw std::runtime_error("cannot compact scheduled streaming regions");
        auto order = dependencyOrder();
        std::set<size_t> keep(order.begin(), order.end());
        for (const auto& port : ports) if (port.input)
            for (auto bit : resolved(port.bits)) if (bit > 1) keep.insert(owner(bit));
        for (const auto& state : states)
            for (auto bit : state.bits) if (bit > 1) keep.insert(owner(bit));
        std::map<size_t, size_t> indices;
        for (auto index : keep) indices.emplace(index, indices.size());
        auto remap = [&](Value value) {
            value = resolved(std::move(value));
            for (auto& bit : value) if (bit > 1)
                bit = (indices.at(owner(bit)) + 1) * 64 + 2 + lane(bit);
            return value;
        };
        std::deque<Node> live;
        for (auto index : keep) {
            auto node = nodes[index];
            node.left = remap(std::move(node.left)); node.right = remap(std::move(node.right));
            node.select = remap(std::move(node.select));
            live.push_back(std::move(node));
        }
        for (auto& port : ports) port.bits = remap(std::move(port.bits));
        for (auto& state : states) {
            state.bits = remap(std::move(state.bits)); state.next = remap(std::move(state.next));
            state.trigger = remap(std::move(state.trigger)); state.reset = remap(std::move(state.reset));
            state.resetValue = remap(std::move(state.resetValue));
        }
        for (auto& access : memoryAccesses) {
            access.address = remap(std::move(access.address)); access.enabled = remap(std::move(access.enabled));
        }
        for (auto& write : memoryWrites) {
            write.address = remap(std::move(write.address)); write.data = remap(std::move(write.data));
            write.enabled = remap(std::move(write.enabled));
        }
        nodes = std::move(live); aliases.clear();
    }

    void writeCpp(const std::string& path) {
        if (path.size() >= 6 && path.compare(path.size() - 6, 6, ".graph") == 0) {
            optimize(); compact();
            std::ofstream output(path);
            if (!output) throw std::runtime_error("cannot write graph partition");
            save(output);
            return;
        }
        std::string delimiter = "cpphdl_graph";
        unsigned suffix = 0;
        for (;;) {
            auto marker = ")" + delimiter;
            bool collision = false;
            for (const auto& node : nodes) collision |= node.name.find(marker) != std::string::npos || node.scope.find(marker) != std::string::npos;
            for (const auto& port : ports) collision |= port.name.find(marker) != std::string::npos;
            for (const auto& memory : memories) collision |= memory.name.find(marker) != std::string::npos;
            for (const auto& attribute : attributes)
                collision |= attribute.scope.find(marker) != std::string::npos || attribute.name.find(marker) != std::string::npos || attribute.value.find(marker) != std::string::npos;
            for (const auto& pipeline : pipelines)
                collision |= pipeline.scope.find(marker) != std::string::npos || pipeline.logic.find(marker) != std::string::npos;
            if (!collision) break;
            delimiter = "cpphdl_" + std::to_string(++suffix);
            if (delimiter.size() > 16) throw std::runtime_error("cannot delimit graph records");
        }
        std::ofstream output(path);
        if (!output) throw std::runtime_error("cannot write graph C++");
        output << "#include <cpphdl_graph.h>\ncpphdl::graph::Graph makeGraph() {\n"
                  "cpphdl::graph::Graph graph;\ngraph.load(R\"" << delimiter << "(\n";
        save(output);
        output << ")" << delimiter << "\"); return graph; }\n"
                  "#ifndef CPPHDL_GRAPH_NO_MAIN\nint main(int argc, char** argv) {\n"
                  "if(argc != 2) return 2;\ntry { auto graph = makeGraph(); graph.emit(argv[1]); } catch(const std::exception& error) {\n"
                  "fprintf(stderr, \"%s\\n\", error.what()); return 1; }\n}\n";
        output << "#endif\n";
    }

    void save(std::ostream& output) const {
        auto valueText = [](const Value& value) {
            std::ostringstream result; result << value.size() << ' ';
            for (auto bit : value) result << bit << ' ';
            return result.str();
        };
        output << nodes.size() << '\n';
        for (const auto& node : nodes)
            output << std::quoted(node.op) << ' ' << node.width << ' '
                   << valueText(node.left) << valueText(node.right) << valueText(node.select)
                   << std::quoted(node.name) << '\n';
        output << aliases.size() << '\n';
        for (auto [target, source] : aliases)
            output << target << ' ' << source << '\n';
        output << ports.size() << '\n';
        for (auto& port : ports)
            output << std::quoted(port.name) << ' ' << valueText(port.bits) << port.input << '\n';
        output << states.size() << '\n';
        for (auto& state : states)
            output << valueText(state.bits) << valueText(state.next) << valueText(state.trigger) << '\n';
        output << memories.size() << '\n';
        for (const auto& memory : memories)
            output << std::quoted(memory.name) << ' ' << memory.width << ' ' << memory.depth << '\n';
        output << memoryAccesses.size() << '\n';
        for (const auto& access : memoryAccesses)
            output << access.memory << ' ' << valueText(access.address) << valueText(access.enabled) << access.transaction << '\n';
        output << memoryWrites.size() << '\n';
        for (const auto& write : memoryWrites)
            output << write.memory << ' ' << valueText(write.address) << valueText(write.data) << valueText(write.enabled) << '\n';
        output << "clock_contract " << static_cast<unsigned>(clockContract) << '\n';
        if (clockContract == ClockContract::NamedEdges) {
            output << clocks.size() << '\n';
            for (const auto& clock : clocks) output << std::quoted(clock.name) << ' ' << clock.frequency << '\n';
            for (const auto& state : states) output << state.clock << ' ' << state.falling << '\n';
        }
        output << "events_v1\n";
        for (const auto& state : states) output << valueText(state.reset) << valueText(state.resetValue) << '\n';
        for (const auto& access : memoryAccesses) output << access.clock << ' ' << access.falling << '\n';
        for (const auto& write : memoryWrites) output << write.clock << ' ' << write.falling << '\n';
        output << "scopes_v1\n";
        for (const auto& node : nodes) output << std::quoted(node.scope) << '\n';
        output << attributes.size() << '\n';
        for (const auto& attribute : attributes)
            output << std::quoted(attribute.scope) << ' ' << std::quoted(attribute.name) << ' ' << std::quoted(attribute.value) << '\n';
        output << "streams_v1\n" << pipelines.size() << '\n';
        for (const auto& p : pipelines) {
            output << std::quoted(p.scope) << ' ' << std::quoted(p.logic) << ' '
                   << p.stages << ' ' << p.latency << ' ' << p.generation << '\n';
            output << p.pins.size() << '\n';
            for (const auto& [name, bits] : p.pins) output << std::quoted(name) << ' ' << valueText(bits) << '\n';
            output << p.registers.size() << '\n';
            for (const auto& bits : p.registers) output << valueText(bits) << '\n';
        }
        output << "roms_v1 " << std::count_if(memories.begin(), memories.end(),
            [](const Memory& m) { return !m.contents.empty(); }) << '\n';
        for (size_t i = 0; i < memories.size(); ++i) if (!memories[i].contents.empty()) {
            output << i << ' ' << memories[i].contents.size();
            for (auto word : memories[i].contents) output << ' ' << word;
            output << '\n';
        }
    }

    void load(const char* text) {
        std::istringstream input(text);
        input.exceptions(std::ios::failbit | std::ios::badbit);
        auto readValue = [&]() {
            size_t count; input >> count;
            if (count > 1048576) throw std::runtime_error("graph value too large");
            Value result(count); for (auto& bit : result) input >> bit; return result;
        };
        size_t count; input >> count;
        for (size_t index = 0; index < count; ++index) {
            Node node; input >> std::quoted(node.op) >> node.width;
            node.left = readValue(); node.right = readValue(); node.select = readValue();
            input >> std::quoted(node.name); nodes.push_back(std::move(node));
        }
        input >> count;
        for (size_t index = 0; index < count; ++index) {
            Bit target, source; input >> target >> source;
            if (!aliases.emplace(target,source).second) throw std::runtime_error("duplicate graph alias");
        }
        input >> count;
        for (size_t index = 0; index < count; ++index) {
            Port port; input >> std::quoted(port.name); port.bits=readValue(); input >> port.input;
            ports.push_back(std::move(port));
        }
        input >> count;
        for (size_t index = 0; index < count; ++index) {
            State state; state.bits=readValue(); state.next=readValue(); state.trigger=readValue();
            states.push_back(std::move(state));
        }
        input >> std::ws;
        if (input.eof()) return;
        input >> count;
        for (size_t index = 0; index < count; ++index) {
            Memory memory; input >> std::quoted(memory.name) >> memory.width >> memory.depth;
            memories.push_back(std::move(memory));
        }
        input >> count;
        for (size_t index = 0; index < count; ++index) {
            MemoryAccess access; input >> access.memory;
            access.address = readValue(); access.enabled = readValue(); input >> access.transaction;
            memoryAccesses.push_back(std::move(access));
        }
        input >> count;
        for (size_t index = 0; index < count; ++index) {
            MemoryWrite write; input >> write.memory;
            write.address = readValue(); write.data = readValue(); write.enabled = readValue();
            memoryWrites.push_back(std::move(write));
        }
        // Older graph records have no lifecycle contract and remain readable.
        input >> std::ws;
        if (!input.eof()) {
            std::string tag;
            unsigned contract;
            input >> tag >> contract;
            if (tag != "clock_contract" || contract > 2)
                throw std::runtime_error("invalid graph clock contract");
            clockContract = static_cast<ClockContract>(contract);
            if (clockContract == ClockContract::NamedEdges) {
                input >> count;
                if (count > 1024) throw std::runtime_error("too many graph clocks");
                for (size_t index = 0; index < count; ++index) {
                    Clock clock; input >> std::quoted(clock.name) >> clock.frequency;
                    clocks.push_back(std::move(clock));
                }
                for (auto& state : states) input >> state.clock >> state.falling;
            }
        }
        if (!input.eof()) input >> std::ws;
        if (!input.eof()) {
            std::string tag; input >> tag;
            if (tag != "events_v1") throw std::runtime_error("invalid graph event metadata");
            for (auto& state : states) { state.reset = readValue(); state.resetValue = readValue(); }
            for (auto& access : memoryAccesses) input >> access.clock >> access.falling;
            for (auto& write : memoryWrites) input >> write.clock >> write.falling;
        }
        if (!input.eof()) input >> std::ws;
        if (!input.eof()) {
            std::string tag; input >> tag;
            if (tag != "scopes_v1") throw std::runtime_error("invalid graph scope metadata");
            for (auto& node : nodes) input >> std::quoted(node.scope);
            input >> count;
            for (size_t i = 0; i < count; ++i) {
                ScopeAttribute attribute;
                input >> std::quoted(attribute.scope) >> std::quoted(attribute.name) >> std::quoted(attribute.value);
                attributes.push_back(std::move(attribute));
            }
        }
        if (!input.eof()) input >> std::ws;
        if (!input.eof()) {
            std::string tag; input >> tag;
            if (tag != "streams_v1") throw std::runtime_error("invalid graph stream metadata");
            input >> count;
            if (count > 1024) throw std::runtime_error("too many streaming regions");
            for (size_t i = 0; i < count; ++i) {
                StreamPipeline p;
                input >> std::quoted(p.scope) >> std::quoted(p.logic) >> p.stages >> p.latency >> p.generation;
                size_t size; input >> size;
                for (size_t j = 0; j < size; ++j) { std::string name; input >> std::quoted(name); p.pins[name] = readValue(); }
                input >> size;
                for (size_t j = 0; j < size; ++j) p.registers.push_back(readValue());
                pipelines.push_back(std::move(p));
            }
        }
        if (!input.eof()) input >> std::ws;
        if (!input.eof()) {
            std::string tag; input >> tag >> count;
            if (tag != "roms_v1" || count > memories.size()) throw std::runtime_error("invalid graph ROM metadata");
            for (size_t i = 0; i < count; ++i) {
                size_t identity, words; input >> identity >> words;
                if (identity >= memories.size() || !words || words > 16777216 || !memories[identity].contents.empty())
                    throw std::runtime_error("invalid graph ROM contents");
                auto& contents = memories[identity].contents;
                contents.resize(words);
                for (auto& word : contents) input >> word;
            }
        }
        validateClocks();
    }

    // Shared live dependency ordering, including bitwise-cycle refinement.
    std::vector<size_t> dependencyOrder() {
        std::vector<size_t> order;
        // Word-level dependencies can include discarded sibling bits of a
        // partially written bus. Refine bitwise producers for those false
        // undriven dependencies and artificial cycles, never a real comb loop.
        for (;;) {
            std::vector<unsigned> marks(nodes.size());
            std::vector<size_t> stack;
            std::optional<size_t> cycle;
            bool outputCone = true;
            order.clear();
            std::function<void(Bit)> visit = [&](Bit raw) {
                auto bit = resolve(raw);
                if (bit < 2 || cycle) return;
                auto index = owner(bit);
                if (marks[index] == 2) return;
                if (marks[index] == 1) {
                    cycle = index;
                    for (auto position = std::find(stack.begin(),stack.end(),index); position != stack.end(); ++position) {
                        const auto& candidate = nodes[*position];
                        if (candidate.width > 1 && (candidate.op == "mux" || candidate.op == "and" || candidate.op == "or" || candidate.op == "xor")) {
                            cycle = *position; break;
                        }
                    }
                    return;
                }
                const auto& node = nodes[index];
                if (node.op == "wire") {
                    for (auto position = stack.rbegin(); position != stack.rend(); ++position) {
                        const auto& candidate = nodes[*position];
                        if (candidate.width > 1 && (candidate.op == "mux" || candidate.op == "and" ||
                            candidate.op == "or" || candidate.op == "xor")) {
                            cycle = *position;
                            return;
                        }
                    }
                    if (std::getenv("CPPHDL_GRAPH_TRACE_UNDRIVEN")) {
                        for (auto parent : stack) {
                            const auto& producer = nodes[parent];
                            std::cerr << "undriven dependency " << parent << ' ' << producer.op
                                      << ' ' << producer.name << " scope=" << producer.scope;
                            for (const auto* operand : {&producer.left, &producer.right, &producer.select}) {
                                std::cerr << " [";
                                if (!operand->empty()) {
                                    auto first = resolve(operand->front());
                                    std::cerr << first;
                                    if (first > 1) std::cerr << ':' << nodes[owner(first)].op << ':' << nodes[owner(first)].name;
                                }
                                std::cerr << ']';
                            }
                            std::cerr << '\n';
                        }
                    }
                    throw std::runtime_error("undriven bit: " + node.name);
                }
                if (outputCone && node.hostEffect())
                    throw std::runtime_error("transactional host effect reaches a combinational output");
                marks[index] = 1;
                stack.push_back(index);
                for (auto* value : {&node.left, &node.right, &node.select}) for (auto input : *value) visit(input);
                marks[index] = 2; order.push_back(index);
                stack.pop_back();
            };
            for (const auto& port : ports) if (!port.input) for (auto bit : port.bits) visit(bit);
            for (const auto& access : memoryAccesses) if (!access.transaction) {
                for (auto bit : access.address) visit(bit);
                for (auto bit : access.enabled) visit(bit);
            }
            outputCone = false;
            for (const auto& access : memoryAccesses) if (access.transaction) {
                for (auto bit : access.address) visit(bit);
                for (auto bit : access.enabled) visit(bit);
            }
            for (const auto& write : memoryWrites)
                for (const auto* value : {&write.address, &write.data, &write.enabled})
                    for (auto bit : *value) visit(bit);
            for (const auto& state : states) {
                for (auto bit : state.next) visit(bit);
                for (auto bit : state.trigger) visit(bit);
                for (auto bit : state.reset) visit(bit);
            }
            // A discarded return value does not discard a host-side effect.
            // The left dependency orders calls; select is their execution guard.
            for (size_t index = 0; index < nodes.size(); ++index)
                if (nodes[index].hostEffect() || nodes[index].validation()) visit((index + 1) * 64 + 2);
            if (!cycle) break;
            auto index = *cycle;
            const auto node = nodes[index];
            if (node.width == 1 || (node.op != "mux" && node.op != "and" && node.op != "or" && node.op != "xor"))
                throw std::runtime_error("combinational cycle at " + std::to_string(index) + " " + node.op);
            for (unsigned offset = 0; offset < node.width; ++offset) {
                auto target = (index + 1) * 64 + 2 + offset;
                if (!aliases.count(target))
                    aliases[target] = add(node.op, 1, {node.left[offset]}, {node.right[offset]}, node.select)[0];
            }
        }
        return order;
    }

    // Compatibility entry point; implementation lives in the native backend.
    // hostOverlap keeps observable sinks on the caller so an external host
    // continuation can run while workers prepare inactive register state.
    void emit(const std::string& path, unsigned chunkSize = 0, unsigned threads = CPPHDL_NATIVE_THREADS,
              bool hostOverlap = false);

};
}

#ifndef CPPHDL_GRAPH_CORE_ONLY
#include "cpphdl_graph_native.h"
#endif
