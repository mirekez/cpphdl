#pragma once

#include "cpphdl_graph.h"

namespace cpphdl::graph {
inline void Graph::emit(const std::string& path, unsigned chunkSize) {
        validateClocks();
        if (chunkSize)
            for (const auto& node : nodes) if (node.hostEffect())
                throw std::runtime_error("chunked native evaluation requires explicit host boundaries");
        if (clockContract == ClockContract::NamedEdges) {
            for (const auto& node : nodes) if (node.hostEffect())
                throw std::runtime_error("multiclock host effects are not implemented");
        }
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
        auto order = dependencyOrder();
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
        for (size_t i = 0; i < clocks.size(); ++i)
            output << "bool " << clocks[i].name << " = false;\nbool __cpphdl_previous_clock_" << i << " = false;\n";
        // Memory depth belongs to runtime storage, never to the bit graph or
        // the C++ stack. Reads observe committed rows throughout evaluation.
        for (size_t index = 0; index < memories.size(); ++index) {
            const auto& memory = memories[index];
            if (!memory.contents.empty()) {
                const auto words = (memory.width + 63) / 64;
                output << "inline static constexpr std::array<std::array<uint64_t," << words << ">,"
                       << memory.depth << "> memory" << index << " = {{\n";
                for (size_t row = 0; row < memory.depth; ++row) {
                    output << "{{";
                    for (unsigned word = 0; word < words; ++word)
                        output << (word ? "," : "") << memory.contents[row * words + word] << "ull";
                    output << "}},\n";
                }
                output << "}};\n";
                continue;
            }
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
        if (chunkSize) output << "std::array<uint64_t," << nodes.size() << "> __cpphdl_values{};\n";
        auto valueName = [&](size_t index) {
            return chunkSize ? "__cpphdl_values[" + std::to_string(index) + "]" : "value" + std::to_string(index);
        };
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
                std::string term = names.count(index) ? names.at(index) : valueName(index);
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
        unsigned chunks = 0, chunkNodes = 0;
        if (!chunkSize) output << "template<bool Commit> void evaluate() {\n";
        for (auto index : order) {
            const auto& node = nodes[index];
            if (node.op == "state" || node.op == "input") continue;
            if (chunkSize && chunkNodes % chunkSize == 0) {
                if (chunks) output << "}\n";
                output << "[[gnu::noinline]] void __cpphdl_chunk_" << chunks++ << "() {\n";
            }
            ++chunkNodes;
            if (node.op == "memory_read") {
                auto memory = number(node.right), word = number(node.select);
                if (!memory || *memory >= memories.size() || !word || *word >= (memories[*memory].width + 63) / 64 ||
                    node.left.empty() || node.left.size() > 64 || node.width != std::min<uint64_t>(64, memories[*memory].width - *word * 64))
                    throw std::runtime_error("invalid graph memory read");
                auto address = assembly(node.left);
                output << (chunkSize ? "" : "const uint64_t ") << valueName(index) << " = " << address << " < " << memories[*memory].depth
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
            else if (op == "sar" || op == "slt" || op == "sdiv" || op == "smod") {
                auto sign = [](std::string value, unsigned width) {
                    return "(int64_t(" + value + " << " + std::to_string(64-width) + ") >> " + std::to_string(64-width) + ")";
                };
                if (op == "slt") expr = sign(left, node.left.size()) + " < " + sign(right, node.right.size());
                else if (op == "sar") expr = sign(left, node.left.size()) + " >> (" + right + " < 64 ? " + right + " : 63)";
                else {
                    auto lhs = sign(left, node.left.size()), rhs = sign(right, node.right.size());
                    expr = "!" + right + " ? 0ull : (" + lhs + " == INT64_MIN && " + rhs + " == -1) ? "
                         + (op == "sdiv" ? left : "0ull") + " : uint64_t(" + lhs
                         + (op == "sdiv" ? " / " : " % ") + rhs + ")";
                }
            } else {
                static const std::map<std::string, std::string> operators{{"and","&"},{"or","|"},{"xor","^"},{"add","+"},{"sub","-"},{"mul","*"},{"div","/"},{"mod","%"},{"eq","=="},{"lt","<"}};
                if (!operators.count(op)) throw std::runtime_error("invalid codegen op " + op);
                expr = left + " " + operators.at(op) + " " + right;
                if (op == "div" || op == "mod") expr = right + " ? (" + expr + ") : 0ull";
            }
            output << (chunkSize ? "" : "const uint64_t ") << valueName(index) << " = (" << expr << ") & " << mask(node.width) << "ull;\n";
        }
        if (chunkSize) {
            if (chunks) output << "}\n";
            output << "template<bool Commit> [[gnu::noinline]] void evaluate() {\n";
            for (unsigned index = 0; index < chunks; ++index)
                output << "__cpphdl_chunk_" << index << "();\n";
        }
        auto clockEdge = [&](int clock, bool falling) {
            if (clock < 0) return std::string("true");
            auto previous = "__cpphdl_previous_clock_" + std::to_string(clock);
            auto current = clocks.at(clock).name;
            return "(" + (falling ? "!" + current + " && " + previous : current + " && !" + previous) + ")";
        };
        for (const auto& access : memoryAccesses)
            output << "if(" << (access.transaction ? "Commit && " + clockEdge(access.clock, access.falling) + " && " : "") << assembly(access.enabled)
                   << " && " << assembly(access.address) << " >= " << memories[access.memory].depth
                   << "ull) throw std::out_of_range(\"native graph memory address\");\n";
        for (size_t index = 0; index < memoryWrites.size(); ++index) {
            const auto& write = memoryWrites[index];
            output << "const bool memory_enable" << index << " = " << clockEdge(write.clock, write.falling) << " && " << assembly(write.enabled) << ";\n"
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
            output << "const bool trigger" << index << " = (" << assembly(state.trigger) << " && " << clockEdge(state.clock, state.falling) << ")";
            if (!state.reset.empty()) output << " || " << assembly(state.reset);
            output << ";\n";
            for (unsigned offset = 0; offset < state.bits.size(); offset += 64)
                output << "const uint64_t next" << index << '_' << offset << " = "
                       << (state.reset.empty() ? "" : assembly(state.reset) + " ? " + assembly(slice(state.resetValue, offset, std::min<size_t>(64,state.next.size()-offset))) + " : ")
                       << assembly(slice(state.next, offset, std::min<size_t>(64,state.next.size()-offset))) << ";\n";
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
        for (size_t i = 0; i < clocks.size(); ++i)
            output << "__cpphdl_previous_clock_" << i << " = " << clocks[i].name << ";\n";
        output << "}\n}\nvoid eval(bool commit = false) { if(commit) evaluate<true>(); else evaluate<false>(); }\n"
                  "void step() { evaluate<true>(); evaluate<false>(); }\n};\n}\n";
        fprintf(stderr, "native graph: %zu source nodes, %zu scheduled nodes, %zu state groups\n", nodes.size(), order.size(), states.size());
    }
}
