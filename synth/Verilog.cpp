#include "Verilog.h"
#include "KeepBoxes.h"
#include "BlackBox.h"
#include <filesystem>

namespace cpphdl::synth {
using namespace graph;

static std::string buildVerilog(Graph& graph, const std::string& module, std::string* boxImplementations) {
    graph.validateClocks();
    if (graph.clockContract != ClockContract::RisingEdgeStep && graph.clockContract != ClockContract::NamedEdges)
        throw std::runtime_error("synthesis requires a rising-edge lifecycle graph");
    auto identifier = [](const std::string& name) {
        if (name.empty() || (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_'))
            throw std::runtime_error("invalid synthesis identifier: " + name);
        for (unsigned char c : name) if (!std::isalnum(c) && c != '_')
            throw std::runtime_error("invalid synthesis identifier: " + name);
        return "\\" + name + " ";
    };
    identifier(module);
    std::set<std::string> portNames;
    std::vector<Clock> clocks = graph.clocks.empty() ? std::vector<Clock>{{"clk", 1}} : graph.clocks;
    for (const auto& clock : clocks) portNames.insert(clock.name);
    for (const auto& port : graph.ports) {
        identifier(port.name);
        if (port.bits.empty() || !portNames.insert(port.name).second || port.name.find("__cpphdl_node_") == 0)
            throw std::runtime_error("empty or conflicting synthesis port: " + port.name);
    }
    for (const auto& node : graph.nodes)
        if (node.hostEffect() || node.op.find("host_") == 0)
            throw std::runtime_error("host effects cannot be synthesized: " + node.op);
    for (const auto& state : graph.states) {
        if (state.bits.empty() || state.bits.size() != state.next.size() || state.trigger != Value{1})
            throw std::runtime_error("unsupported synthesis state/event contract");
    }
    graph.optimize();
    auto order = graph.dependencyOrder();
    KeepBoxes boxes(graph);
    auto nodeName = [](size_t index) { return "__cpphdl_node_" + std::to_string(index); };
    // Batch independent bit muxes, never a chain with dependencies inside it.
    // The mapped graph/cell inventory stays bit-level; only its spelling changes.
    std::vector<std::vector<size_t>> bundles;
    std::map<size_t, std::pair<size_t, unsigned>> bundled;
    if (boxes.boxes.empty()) {
        std::vector<size_t> depth(graph.nodes.size());
        std::map<std::pair<size_t, Bit>, std::vector<size_t>> candidates;
        for (auto index : order) {
            const auto& node = graph.nodes[index];
            if (node.op == "input" || node.op == "state") continue;
            for (const auto* operand : {&node.left, &node.right, &node.select})
                for (auto bit : graph.resolved(*operand)) if (bit > 1)
                    depth[index] = std::max(depth[index], depth[Graph::owner(bit)] + 1);
            if (node.op == "mux" && node.width == 1 && node.select.size() == 1)
                candidates[{depth[index], graph.resolved(node.select)[0]}].push_back(index);
        }
        for (const auto& [key, nodes] : candidates) for (size_t start = 0; start < nodes.size(); start += 64) {
            auto count = std::min<size_t>(64, nodes.size() - start);
            if (count < 2) continue;
            auto group = bundles.size();
            bundles.emplace_back(nodes.begin() + start, nodes.begin() + start + count);
            for (unsigned lane = 0; lane < count; ++lane) bundled[nodes[start + lane]] = {group, lane};
        }
    }
    auto bundleName = [](size_t index) { return "__cpphdl_node_bundle_" + std::to_string(index); };
    auto reference = [&](Bit bit) {
        auto owner = Graph::owner(bit);
        if (auto found = bundled.find(owner); found != bundled.end())
            return std::make_pair(bundleName(found->second.first), found->second.second);
        return std::make_pair(nodeName(owner), unsigned(Graph::lane(bit)));
    };
    auto bits = [&](Value value) {
        if (value.empty()) throw std::runtime_error("empty synthesis operand");
        value = graph.resolved(value);
        std::vector<std::string> parts;
        for (size_t end = value.size(); end;) {
            size_t start = end - 1;
            if (value[start] < 2) {
                while (start && value[start - 1] < 2) --start;
                std::string literal = std::to_string(end - start) + "'b";
                for (size_t i = end; i-- > start;) literal += value[i] ? '1' : '0';
                parts.push_back(std::move(literal));
            } else {
                auto [name, high] = reference(value[start]);
                unsigned low = high;
                while (start && value[start - 1] > 1) {
                    auto previous = reference(value[start - 1]);
                    if (previous.first != name || previous.second + 1 != low) break;
                    --start; low = previous.second;
                }
                parts.push_back(name + "[" + std::to_string(high) +
                    (high == low ? "" : ":" + std::to_string(low)) + "]");
            }
            end = start;
        }
        if (parts.size() == 1) return parts.front();
        std::string result = "{";
        for (const auto& part : parts) { if (result.size() > 1) result += ','; result += part; }
        return result + "}";
    };
    auto memoryIndex = [&](size_t memory, const Value& address) {
        auto depth = graph.memories.at(memory).depth;
        if (depth > (uint64_t(1) << 32)) throw std::runtime_error("synthesis memory exceeds 32-bit index range");
        unsigned width = 1;
        while ((uint64_t(1) << width) < depth) ++width;
        return bits(slice(address, 0, width));
    };
    std::ostringstream out;
    out << "// C++HDL operation graph; explicit register clock/edge ownership.\n"
        << "module " << identifier(module) << "(";
    for (size_t i = 0; i < clocks.size(); ++i)
        out << (i ? ",\n" : "") << "input wire " << identifier(clocks[i].name);
    for (const auto& port : graph.ports)
        out << ",\n" << (port.input ? "input" : "output") << " wire [" << port.bits.size()-1 << ":0] " << identifier(port.name);
    out << ");\n";
    // Keep original node IDs stable for source/debug correspondence.
    std::set<size_t> declarations(order.begin(), order.end());
    for (const auto& port : graph.ports) for (auto bit : graph.resolved(port.bits))
        if (bit > 1) declarations.insert(Graph::owner(bit));
    for (const auto& state : graph.states) for (auto bit : graph.resolved(state.bits))
        if (bit > 1) declarations.insert(Graph::owner(bit));
    for (size_t i : declarations) if (!bundled.count(i))
        out << (graph.nodes[i].op == "state" ? "reg" : "wire") << " [" << graph.nodes[i].width-1
            << ":0] " << nodeName(i) << ";\n";
    for (size_t i = 0; i < bundles.size(); ++i)
        out << "wire [" << bundles[i].size() - 1 << ":0] " << bundleName(i) << ";\n";
    for (size_t i = 0; i < graph.memories.size(); ++i) {
        out << "reg [" << graph.memories[i].width - 1 << ":0] __cpphdl_memory_" << i
            << " [0:" << graph.memories[i].depth - 1 << "];\n";
        const auto& memory = graph.memories[i];
        if (!memory.contents.empty()) {
            out << "initial begin\n";
            const auto words = (memory.width + 63) / 64;
            for (size_t row = 0; row < memory.depth; ++row) {
                out << "  __cpphdl_memory_" << i << '[' << row << "] = " << memory.width << "'b";
                for (unsigned bit = memory.width; bit-- > 0;)
                    out << ((memory.contents[row * words + bit / 64] >> (bit % 64)) & 1);
                out << ";\n";
            }
            out << "end\n";
        }
    }
    for (const auto& port : graph.ports) {
        if (port.input) out << "assign " << bits(port.bits) << " = " << identifier(port.name) << ";\n";
        else out << "assign " << identifier(port.name) << " = " << bits(port.bits) << ";\n";
    }
    const std::map<std::string, std::string> operators{
        {"and","&"}, {"or","|"}, {"xor","^"}, {"add","+"}, {"sub","-"},
        {"mul","*"}, {"div","/"}, {"mod","%"}, {"eq","=="}, {"lt","<"},
        {"shl","<<"}, {"shr",">>"}};
    std::vector<std::ostringstream> boxBodies(boxes.boxes.size());
    for (size_t group = 0; group < bundles.size(); ++group) {
        Value left, right;
        for (auto index : bundles[group]) {
            left.push_back(graph.nodes[index].left.at(0));
            right.push_back(graph.nodes[index].right.at(0));
        }
        out << "assign " << bundleName(group) << " = "
            << bits(graph.nodes[bundles[group][0]].select) << " ? " << bits(left) << " : " << bits(right) << ";\n";
    }
    for (auto index : order) {
        const auto& node = graph.nodes[index];
        if (node.op == "state" || node.op == "input" || bundled.count(index)) continue;
        if (node.op == "blackbox") {
            blackBoxDelay(node);
            auto& destination = boxes.owner.count(index) ? boxBodies[boxes.owner.at(index)] : out;
            destination << "(* keep = 1, keep_hierarchy = 1 *) " << identifier(node.name)
                << "#(.INPUT_BITS(" << node.left.size() << "), .OUTPUT_BITS(" << node.width << ")) external_" << index
                << "(.args(" << bits(node.left) << "), .result(" << nodeName(index) << "));\n";
            continue;
        }
        std::string expr;
        auto left = bits(node.left);
        if (node.op == "memory_read") {
            auto memory = number(node.right), word = number(node.select);
            if (!memory || *memory >= graph.memories.size() || !word ||
                *word * 64 >= graph.memories[*memory].width ||
                node.width != std::min<uint64_t>(64, graph.memories[*memory].width - *word * 64))
                throw std::runtime_error("invalid synthesis memory read");
            expr = left + " < 64'd" + std::to_string(graph.memories[*memory].depth) + " ? __cpphdl_memory_" +
                std::to_string(*memory) + "[" + memoryIndex(*memory, node.left) + "][" + std::to_string(*word * 64 + node.width - 1) + ":" +
                std::to_string(*word * 64) + "] : " + std::to_string(node.width) + "'d0";
        }
        else if (node.op == "mux") expr = bits(node.select) + " ? " + left + " : " + bits(node.right);
        else if (node.op == "any") expr = "|" + left;
        else if (node.op == "all") expr = "&" + left;
        else if (node.op == "parity") expr = "^" + left;
        else if (node.op == "slt") expr = "$signed(" + left + ") < $signed(" + bits(node.right) + ")";
        else if (node.op == "sar") expr = "$signed(" + left + ") >>> " + bits(node.right);
        else if (node.op == "sdiv" || node.op == "smod")
            // Isolate signed arithmetic from the unsigned zero arm's context.
            expr = bits(node.right) + " != 0 ? $unsigned($signed(" + left + ") " +
                (node.op == "sdiv" ? "/" : "%") + " $signed(" + bits(node.right) + ")) : " +
                std::to_string(node.width) + "'d0";
        else if (operators.count(node.op)) {
            expr = left + " " + operators.at(node.op) + " " + bits(node.right);
            // The graph defines unsigned division by zero as zero, not X.
            if (node.op == "div" || node.op == "mod")
                expr = bits(node.right) + " != 0 ? (" + expr + ") : " + std::to_string(node.width) + "'d0";
        } else throw std::runtime_error("unsupported synthesis operation: " + node.op);
        auto& destination = boxes.owner.count(index) ? boxBodies[boxes.owner.at(index)] : out;
        destination << "assign " << nodeName(index) << " = " << expr << ";\n";
    }
    std::ostringstream implementations;
    for (size_t b = 0; b < boxes.boxes.size(); ++b) {
        const auto& box = boxes.boxes[b];
        if (box.outputs.empty()) continue;
        out << "(* keep_hierarchy = 1 *) " << box.module << " box_" << b << "(";
        if (!box.inputs.empty()) out << ".box_in(" << bits(box.inputs) << "), ";
        out << ".box_out(" << bits(box.outputs) << "));\n";
        implementations << "// Preserved C++ module " << box.scope << ", delay " << box.delay << " ns.\n"
                        << "(* keep_hierarchy = 1, cpphdl_keep_box = 1 *)\nmodule " << box.module << "(";
        if (!box.inputs.empty()) implementations << "input wire [" << box.inputs.size()-1 << ":0] box_in, ";
        implementations << "output wire [" << box.outputs.size()-1 << ":0] box_out);\n";
        std::set<size_t> declarations(box.nodes.begin(), box.nodes.end());
        for (auto bit : box.inputs) declarations.insert(Graph::owner(bit));
        for (auto n : declarations) implementations << "wire [" << graph.nodes[n].width-1 << ":0] " << nodeName(n) << ";\n";
        if (!box.inputs.empty()) implementations << "assign " << bits(box.inputs) << " = box_in;\n";
        implementations << boxBodies[b].str() << "assign box_out = " << bits(box.outputs) << ";\nendmodule\n";
    }
    for (size_t clock = 0; clock < clocks.size(); ++clock) for (bool falling : {false, true}) {
        std::vector<const State*> states;
        for (const auto& state : graph.states)
            if (size_t(state.clock < 0 ? 0 : state.clock) == clock && state.falling == falling) states.push_back(&state);
        // Separate resettable and non-resettable registers even within a domain.
        for (const auto* entry : states) {
            const auto& state = *entry;
            std::string reset;
            if (!state.reset.empty()) {
                for (const auto& port : graph.ports)
                    if (port.input && graph.resolved(port.bits) == graph.resolved(state.reset)) reset = identifier(port.name);
                if (reset.empty()) throw std::runtime_error("asynchronous reset input port not found");
            }
            out << "always @(" << (falling ? "negedge " : "posedge ") << identifier(clocks[clock].name);
            if (!reset.empty()) out << " or posedge " << reset;
            out << ") begin\n";
            if (!reset.empty())
                out << "  if (" << reset << ") " << bits(state.bits) << " <= " << bits(state.resetValue) << ";\n  else ";
            out << "  " << bits(state.bits) << " <= " << bits(state.next) << ";\nend\n";
        }
        bool opened = false;
        for (const auto& write : graph.memoryWrites) {
            if (size_t(write.clock < 0 ? 0 : write.clock) != clock || write.falling != falling) continue;
            if (!opened) {
                out << "always @(" << (falling ? "negedge " : "posedge ") << identifier(clocks[clock].name) << ") begin\n";
                opened = true;
            }
            out << "  if (" << bits(write.enabled) << " && " << bits(write.address) << " < 64'd" << graph.memories[write.memory].depth
                << ") __cpphdl_memory_" << write.memory << "[" << memoryIndex(write.memory, write.address) << "] <= " << bits(write.data) << ";\n";
        }
        if (opened) out << "end\n";
    }
    out << "endmodule\n";
    out << implementations.str();
    if (boxImplementations) *boxImplementations = implementations.str();
    return out.str();
}

std::string verilogText(Graph& graph, const std::string& module) {
    return buildVerilog(graph,module,nullptr);
}

void emitVerilog(Graph& graph, const std::string& path, const std::string& module) {
    std::string implementations;
    auto text = buildVerilog(graph,module,&implementations);
    std::ofstream file(path);
    if (!file || !(file << text)) throw std::runtime_error("cannot write synthesis Verilog");
    if (!implementations.empty()) {
        std::ofstream bodies(std::filesystem::path(path).parent_path() / "keep_boxes.v");
        if (!bodies || !(bodies << implementations))
            throw std::runtime_error("cannot write keep box implementations");
    }
    std::set<std::string> external;
    for (auto n : graph.dependencyOrder()) if (graph.nodes[n].op == "blackbox") external.insert(graph.nodes[n].name);
    if (!external.empty()) {
        std::ofstream stubs(std::filesystem::path(path).parent_path() / "blackboxes.v");
        stubs << "// External implementation required. Do not use these empty declarations as simulation models.\n";
        for (const auto& name : external)
            stubs << "(* blackbox = 1, keep_hierarchy = 1 *) module \\" << name << ' '
                  << " #(parameter INPUT_BITS=64, OUTPUT_BITS=64) (input wire [INPUT_BITS-1:0] args, output wire [OUTPUT_BITS-1:0] result);\nendmodule\n";
        if (!stubs) throw std::runtime_error("cannot write blackbox declarations");
    }
}
}
