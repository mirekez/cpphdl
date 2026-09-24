#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
    bool hostEffect() const { return op == "host_random" || op == "host_jtag_tick" || op == "host_debug_tick"; }
};
struct Port { std::string name; Value bits; bool input; };
struct State { Value bits, next, trigger; };
struct Memory { std::string name; unsigned width; uint64_t depth; };
struct MemoryAccess { size_t memory; Value address, enabled; bool transaction; };
struct MemoryWrite { size_t memory; Value address, data, enabled; };

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
    std::vector<Node> nodes;
    std::map<Bit, Bit> aliases;
    std::vector<Port> ports;
    std::vector<State> states;
    std::vector<Memory> memories;
    std::vector<MemoryAccess> memoryAccesses;
    std::vector<MemoryWrite> memoryWrites;

    Value add(std::string op, unsigned width, Value left = {}, Value right = {},
              Value select = {}, std::string name = {}) {
        if (!width || width > 64) throw std::runtime_error("invalid graph node width");
        auto base = (nodes.size() + 1) * 64 + 2;
        nodes.push_back({std::move(op), width, std::move(left), std::move(right),
                         std::move(select), std::move(name)});
        Value result(width);
        for (unsigned index = 0; index < width; ++index) result[index] = base + index;
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
    Value binary(std::string op, Value left, Value right, unsigned width) {
        if (op == "and" || op == "or" || op == "xor" || op == "mux") {
            left = resize(left, width); right = resize(right, width);
            Value result;
            for (unsigned offset = 0; offset < width;) {
                unsigned count = std::min(64u, width - offset);
                auto part = add(op, count, slice(left, offset, count), slice(right, offset, count));
                result.insert(result.end(), part.begin(), part.end());
                offset += count;
            }
            simplifyLast(result);
            return resolved(result);
        }
        if (left.size() > 64 || right.size() > 64 || width > 64)
            throw std::runtime_error("wide arithmetic not supported: " + op);
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
        simplifyLast(result);
        return resolved(result);
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
        if (op == "eq") return left == right;
        if (op == "lt") return left < right;
        if (op == "slt") return signedNumber(left, width) < signedNumber(right, width);
        if (op == "shl") return right < 64 ? left << right : 0;
        if (op == "shr") return right < 64 ? left >> right : 0;
        if (op == "sar") return uint64_t(signedNumber(left, width) >> std::min<uint64_t>(right, 63));
        throw std::runtime_error("unsupported graph operation: " + op);
    }

    void simplifyLast(const Value& value) {
        std::set<size_t> owners;
        for (auto bit : value) if (bit > 1) owners.insert(owner(bit));
        for (auto index : owners) simplify(index);
    }
    bool simplify(size_t index) {
        auto& node = nodes[index];
        node.left = resolved(node.left); node.right = resolved(node.right);
        node.select = resolved(node.select);
        if (node.hostEffect() || node.op == "memory_read") return false;
        bool changed = false;
        auto redirect = [&](unsigned offset, Bit bit) {
            Bit target = (index + 1) * 64 + 2 + offset;
            if (!aliases.count(target) && target != bit) { aliases[target] = bit; changed = true; }
        };
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
        return changed;
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
            if (node.op == "wire" || node.op == "input" || node.op == "state" || node.hostEffect()) continue;
            node.left = resolved(node.left); node.right = resolved(node.right); node.select = resolved(node.select);
            std::ostringstream key;
            key << node.op << ':' << node.width;
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

    void writeCpp(const std::string& path) const {
        std::string delimiter = "cpphdl_graph";
        unsigned suffix = 0;
        for (;;) {
            auto marker = ")" + delimiter;
            bool collision = false;
            for (const auto& node : nodes) collision |= node.name.find(marker) != std::string::npos;
            for (const auto& port : ports) collision |= port.name.find(marker) != std::string::npos;
            for (const auto& memory : memories) collision |= memory.name.find(marker) != std::string::npos;
            if (!collision) break;
            delimiter = "cpphdl_" + std::to_string(++suffix);
            if (delimiter.size() > 16) throw std::runtime_error("cannot delimit graph records");
        }
        std::ofstream output(path);
        if (!output) throw std::runtime_error("cannot write graph C++");
        output << "#include <cpphdl_graph.h>\nint main(int argc, char** argv) {\n"
                  "if(argc != 2) return 2;\ncpphdl::graph::Graph graph;\n"
                  "try { graph.load(R\"" << delimiter << "(\n";
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
        output << ")" << delimiter << "\"); graph.emit(argv[1]); } catch(const std::exception& error) {\n"
                  "fprintf(stderr, \"%s\\n\", error.what()); return 1; }\n}\n";
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
    }

    void emit(const std::string& path) {
        for (const auto& memory : memories)
            if (!memory.width || memory.width > 1048576 || !memory.depth)
                throw std::runtime_error("invalid graph memory dimensions");
        for (const auto& access : memoryAccesses)
            if (access.memory >= memories.size() || access.address.empty() || access.address.size() > 64 || access.enabled.size() != 1)
                throw std::runtime_error("invalid graph memory access");
        for (const auto& write : memoryWrites)
            if (write.memory >= memories.size() || write.data.size() != memories[write.memory].width ||
                write.address.empty() || write.address.size() > 64 || write.enabled.size() != 1)
                throw std::runtime_error("invalid graph memory write");
        for (const auto& port : ports) {
            if (port.name.empty() || (!std::isalpha(static_cast<unsigned char>(port.name[0])) && port.name[0] != '_'))
                throw std::runtime_error("top port is not a C++ identifier");
            for (unsigned char character : port.name)
                if (!std::isalnum(character) && character != '_')
                    throw std::runtime_error("top port is not a C++ identifier");
        }
        optimize();
        // One simultaneous NBA commit followed by a combinational evaluation
        // suffices only when events cannot be created by that same commit.
        // Reject state-derived clocks/resets instead of silently losing deltas.
        std::set<size_t> eventCone;
        std::function<void(Bit)> checkEvent = [&](Bit raw) {
            auto bit = resolve(raw);
            if (bit < 2) return;
            auto index = owner(bit);
            if (!eventCone.insert(index).second) return;
            const auto& node = nodes[index];
            if (node.op == "state") throw std::runtime_error("state-derived clock/reset requires delta scheduling");
            for (auto* value : {&node.left,&node.right,&node.select}) for (auto input : *value) checkEvent(input);
        };
        for (const auto& state : states)
            if (nodes[owner(state.bits[0])].name == "event history")
                for (auto bit : state.next) checkEvent(bit);
        std::vector<size_t> order;
        // A word-level cycle can be artificial (independent fields in a bus).
        // Split only cyclic bitwise producers; never iterate a real comb loop.
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
                if (node.op == "wire") throw std::runtime_error("undriven bit: " + node.name);
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
            }
            // A discarded return value does not discard a host-side effect.
            // The left dependency orders calls; select is their execution guard.
            for (size_t index = 0; index < nodes.size(); ++index)
                if (nodes[index].hostEffect()) visit((index + 1) * 64 + 2);
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
        std::ofstream output(path);
        if (!output) throw std::runtime_error("cannot write native model");
        output << "#pragma once\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_random"; }))
            output << "#include <cstdlib>\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_jtag_tick"; }))
            output << "extern \"C\" int jtag_tick(unsigned char*, unsigned char*, unsigned char*, unsigned char*, unsigned char);\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_debug_tick"; }))
            output << "extern \"C\" int debug_tick(unsigned char*, unsigned char, int*, int*, int*, unsigned char, unsigned char*, int, int);\n";
        output << "#include <array>\n#include <cstdint>\n#include <stdexcept>\n#include <vector>\n"
                  "namespace cpphdl_native {\nstruct Model {\n";
        // Memory depth belongs to runtime storage, never to the bit graph or
        // the C++ stack. Reads observe committed rows throughout evaluation.
        for (size_t index = 0; index < memories.size(); ++index) {
            auto type = "std::vector<std::array<uint64_t," + std::to_string((memories[index].width + 63) / 64) + ">>";
            output << type << " memory" << index << " = " << type << '(' << memories[index].depth << "ull);\n";
        }
        std::map<size_t, std::string> names;
        for (const auto& port : ports) {
            output << "std::array<uint32_t," << (port.bits.size()+31)/32 << "> " << port.name << "{};\n";
            if (port.input) for (unsigned offset = 0; offset < port.bits.size(); offset += 64) {
                auto count = std::min<size_t>(64, port.bits.size() - offset);
                std::string expr = "uint64_t(" + port.name + "[" + std::to_string(offset/32) + "])";
                if (count > 32) expr += " | (uint64_t(" + port.name + "[" + std::to_string(offset/32+1) + "]) << 32)";
                names[owner(port.bits[offset])] = "(" + expr + ")";
            }
        }
        for (size_t index = 0; index < nodes.size(); ++index) if (nodes[index].op == "state") {
            names[index] = "state" + std::to_string(index);
            output << "uint64_t " << names[index] << " = " << number(nodes[index].left).value_or(0) << "ull;\n";
        }
        auto assembly = [&](Value value) {
            value = resolved(value);
            if (value.size() > 64) throw std::runtime_error("wide assembly");
            std::string result;
            uint64_t constants = 0;
            for (unsigned offset = 0; offset < value.size();) {
                auto bit = value[offset];
                if (bit < 2) { constants |= bit << offset; ++offset; continue; }
                auto index = owner(bit); auto first = lane(bit);
                unsigned count = 1;
                while (offset + count < value.size() && value[offset+count] == bit+count && first+count < nodes[index].width) ++count;
                std::string term = names.count(index) ? names.at(index) : "value" + std::to_string(index);
                if (first) term = "(" + term + " >> " + std::to_string(first) + ")";
                if (count < 64) term = "(" + term + " & " + std::to_string(mask(count)) + "ull)";
                if (offset) term = "(" + term + " << " + std::to_string(offset) + ")";
                if (!result.empty()) result += " | "; result += term; offset += count;
            }
            if (constants || result.empty()) { if (!result.empty()) result += " | "; result += std::to_string(constants) + "ull"; }
            return "(" + result + ")";
        };
        // Output settling does not need next-state logic. Make the two phases
        // compile-time distinct so the host compiler can remove that work,
        // rather than testing a runtime flag after computing both schedules.
        output << "template<bool Commit> void evaluate() {\n";
        for (auto index : order) {
            const auto& node = nodes[index];
            if (node.op == "state" || node.op == "input") continue;
            if (node.op == "memory_read") {
                auto memory = number(node.right), word = number(node.select);
                if (!memory || *memory >= memories.size() || !word || *word >= (memories[*memory].width + 63) / 64 ||
                    node.left.empty() || node.left.size() > 64 || node.width != std::min<uint64_t>(64, memories[*memory].width - *word * 64))
                    throw std::runtime_error("invalid graph memory read");
                auto address = assembly(node.left);
                output << "const uint64_t value" << index << " = " << address << " < " << memories[*memory].depth
                       << "ull ? memory" << *memory << '[' << address << "][" << *word << "] : 0ull;\n";
                continue;
            }
            if (node.op == "host_debug_tick") {
                if (node.width != 32 || node.right.size() != 192 || node.select.size() != 1)
                    throw std::runtime_error("invalid debug_tick effect layout");
                const unsigned sizes[] = {8, 8, 32, 32, 32, 8, 8, 32, 32};
                const bool pointers[] = {true, false, true, true, true, false, true, false, false};
                auto prefix = "debug" + std::to_string(index);
                unsigned offset = 0;
                // Initialize pointer locals from current storage: a callback
                // may intentionally leave outputs unchanged on a stalled tick.
                for (unsigned argument = 0; argument < 9; ++argument) {
                    auto type = sizes[argument] == 8 ? "unsigned char" : "int";
                    output << type << ' ' << prefix << '_' << argument << " = static_cast<"
                           << type << ">(" << assembly(slice(node.right, offset, sizes[argument])) << ");\n";
                    offset += sizes[argument];
                }
                output << "uint64_t value" << index << " = 0;\n"
                       << "if constexpr(Commit) { if(" << assembly(node.select) << ") {\n"
                       << "value" << index << " = uint32_t(::debug_tick(";
                for (unsigned argument = 0; argument < 9; ++argument) {
                    if (argument) output << ", ";
                    if (pointers[argument]) output << '&';
                    output << prefix << '_' << argument;
                }
                output << "));\n} }\n";
                continue;
            }
            if (node.op == "host_debug_output") {
                auto argument = number(node.select);
                if (node.left.size() != 1 || node.left.front() < 2 || !argument ||
                    (*argument != 0 && *argument != 2 && *argument != 3 && *argument != 4 && *argument != 6) ||
                    nodes[owner(node.left.front())].op != "host_debug_tick" ||
                    node.width != (*argument == 0 || *argument == 6 ? 8u : 32u))
                    throw std::runtime_error("invalid debug_tick output projection");
                output << "const uint64_t value" << index << " = uint32_t(debug"
                       << owner(node.left.front()) << '_' << *argument << ");\n";
                continue;
            }
            auto left = assembly(node.left), right = assembly(node.right);
            if (node.op == "host_jtag_tick") {
                if (node.width != 64 || node.right.size() != 40 || node.select.size() != 1)
                    throw std::runtime_error("invalid jtag_tick effect layout");
                auto prefix = "jtag" + std::to_string(index);
                output << "uint64_t value" << index << " = (" << right << " & 4294967295ull) << 32;\n"
                       << "if constexpr(Commit) { if(" << assembly(node.select) << ") {\n";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << "unsigned char " << prefix << '_' << argument << " = static_cast<unsigned char>("
                           << assembly(slice(node.right, 8 * argument, 8)) << ");\n";
                output << "const int " << prefix << "_result = ::jtag_tick(";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << '&' << prefix << '_' << argument << ", ";
                output << "static_cast<unsigned char>(" << assembly(slice(node.right, 32, 8)) << "));\n"
                       << "value" << index << " = uint64_t(uint32_t(" << prefix << "_result))";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << " | (uint64_t(" << prefix << '_' << argument << ") << " << 32 + 8 * argument << ')';
                output << ";\n} }\n";
                continue;
            }
            std::string expr;
            auto op = node.op;
            if (op == "host_random") expr = "Commit && " + assembly(node.select) + " ? uint64_t(::random()) : 0ull";
            else if (op == "mux") expr = assembly(node.select) + " ? " + left + " : " + right;
            else if (op == "any") expr = left + " != 0";
            else if (op == "all") expr = left + " == " + std::to_string(mask(node.left.size())) + "ull";
            else if (op == "parity") expr = "__builtin_parityll(" + left + ")";
            else if (op == "shl" || op == "shr") expr = right + " < 64 ? " + left + (op == "shl" ? " << " : " >> ") + right + " : 0ull";
            else if (op == "sar" || op == "slt") {
                auto sign = [](std::string value, unsigned width) {
                    return "(int64_t(" + value + " << " + std::to_string(64-width) + ") >> " + std::to_string(64-width) + ")";
                };
                if (op == "slt") expr = sign(left, node.left.size()) + " < " + sign(right, node.right.size());
                else expr = sign(left, node.left.size()) + " >> (" + right + " < 64 ? " + right + " : 63)";
            } else {
                static const std::map<std::string, std::string> operators{{"and","&"},{"or","|"},{"xor","^"},{"add","+"},{"sub","-"},{"mul","*"},{"div","/"},{"mod","%"},{"eq","=="},{"lt","<"}};
                if (!operators.count(op)) throw std::runtime_error("invalid codegen op " + op);
                expr = left + " " + operators.at(op) + " " + right;
                if (op == "div" || op == "mod") expr = right + " ? (" + expr + ") : 0ull";
            }
            output << "const uint64_t value" << index << " = (" << expr << ") & " << mask(node.width) << "ull;\n";
        }
        for (const auto& access : memoryAccesses)
            output << "if(" << (access.transaction ? "Commit && " : "") << assembly(access.enabled)
                   << " && " << assembly(access.address) << " >= " << memories[access.memory].depth
                   << "ull) throw std::out_of_range(\"native graph memory address\");\n";
        for (size_t index = 0; index < memoryWrites.size(); ++index) {
            const auto& write = memoryWrites[index];
            output << "const bool memory_enable" << index << " = " << assembly(write.enabled) << ";\n"
                   << "const uint64_t memory_address" << index << " = " << assembly(write.address) << ";\n";
            output << "if(Commit && memory_enable" << index << " && memory_address" << index << " >= "
                   << memories[write.memory].depth << "ull) throw std::out_of_range(\"native graph memory write address\");\n";
            for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                output << "const uint64_t memory_next" << index << '_' << offset / 64 << " = "
                       << assembly(slice(write.data, offset, std::min<size_t>(64, write.data.size() - offset))) << ";\n";
        }
        for (const auto& port : ports) if (!port.input)
            for (unsigned offset = 0; offset < port.bits.size(); offset += 32)
                output << port.name << '[' << offset/32 << "] = uint32_t(" << assembly(slice(port.bits, offset, std::min<size_t>(32,port.bits.size()-offset))) << ");\n";
        for (size_t index = 0; index < states.size(); ++index) {
            auto& state = states[index];
            output << "const bool trigger" << index << " = " << assembly(state.trigger) << ";\n";
            for (unsigned offset = 0; offset < state.bits.size(); offset += 64)
                output << "const uint64_t next" << index << '_' << offset << " = " << assembly(slice(state.next, offset, std::min<size_t>(64,state.next.size()-offset))) << ";\n";
        }
        output << "if constexpr(Commit) {\n";
        // Snapshot all write operands before any state commit. Ordered writes
        // to one address retain the C++ queue's last-write-wins semantics.
        for (size_t index = 0; index < memoryWrites.size(); ++index) {
            const auto& write = memoryWrites[index];
            output << "if(memory_enable" << index << ") {\n";
            for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                output << "memory" << write.memory << "[memory_address" << index << "][" << offset / 64
                       << "] = memory_next" << index << '_' << offset / 64 << ";\n";
            output << "}\n";
        }
        for (size_t index = 0; index < states.size(); ++index) {
            auto& state = states[index];
            output << "if(trigger" << index << ") {\n";
            for (unsigned offset = 0; offset < state.bits.size(); offset += 64)
                output << names.at(owner(state.bits[offset])) << " = next" << index << '_' << offset << ";\n";
            output << "}\n";
        }
        output << "}\n}\nvoid eval(bool commit = false) { if(commit) evaluate<true>(); else evaluate<false>(); }\n"
                  "void step() { evaluate<true>(); evaluate<false>(); }\n};\n}\n";
        fprintf(stderr, "native graph: %zu source nodes, %zu scheduled nodes, %zu state groups\n", nodes.size(), order.size(), states.size());
    }
};
}
