#include "Verilog.h"
#include "KeepBoxes.h"
#include <filesystem>

namespace cpphdl::synth {
using namespace graph;

void emitVerilog(Graph& graph, const std::string& path, const std::string& module) {
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
    auto bits = [&](Value value) {
        if (value.empty()) throw std::runtime_error("empty synthesis operand");
        value = graph.resolved(value);
        std::string result = "{";
        for (size_t i = value.size(); i-- > 0;) {
            if (i + 1 != value.size()) result += ",";
            auto bit = value[i];
            result += bit < 2 ? (bit ? "1'b1" : "1'b0") :
                nodeName(Graph::owner(bit)) + "[" + std::to_string(Graph::lane(bit)) + "]";
        }
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
    out << "// CppHDL operation graph; explicit register clock/edge ownership.\n"
        << "module " << identifier(module) << "(";
    for (size_t i = 0; i < clocks.size(); ++i)
        out << (i ? ",\n" : "") << "input wire " << identifier(clocks[i].name);
    for (const auto& port : graph.ports)
        out << ",\n" << (port.input ? "input" : "output") << " wire [" << port.bits.size()-1 << ":0] " << identifier(port.name);
    out << ");\n";
    // Keep original node IDs stable for source/debug correspondence.
    for (size_t i = 0; i < graph.nodes.size(); ++i)
        out << (graph.nodes[i].op == "state" ? "reg" : "wire") << " [" << graph.nodes[i].width-1
            << ":0] " << nodeName(i) << ";\n";
    for (size_t i = 0; i < graph.memories.size(); ++i)
        out << "reg [" << graph.memories[i].width - 1 << ":0] __cpphdl_memory_" << i
            << " [0:" << graph.memories[i].depth - 1 << "];\n";
    for (const auto& port : graph.ports) {
        if (port.input) out << "assign " << bits(port.bits) << " = " << identifier(port.name) << ";\n";
        else out << "assign " << identifier(port.name) << " = " << bits(port.bits) << ";\n";
    }
    const std::map<std::string, std::string> operators{
        {"and","&"}, {"or","|"}, {"xor","^"}, {"add","+"}, {"sub","-"},
        {"mul","*"}, {"div","/"}, {"mod","%"}, {"eq","=="}, {"lt","<"},
        {"shl","<<"}, {"shr",">>"}};
    std::vector<std::ostringstream> boxBodies(boxes.boxes.size());
    for (auto index : order) {
        const auto& node = graph.nodes[index];
        if (node.op == "state" || node.op == "input") continue;
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
            out << "always @(" << (falling ? "negedge " : "posedge ") << identifier(clocks[clock].name);
            if (!state.reset.empty()) out << " or posedge " << bits(state.reset);
            out << ") begin\n";
            if (!state.reset.empty())
                out << "  if (" << bits(state.reset) << ") " << bits(state.bits) << " <= " << bits(state.resetValue) << ";\n  else ";
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
    std::ofstream file(path);
    if (!file || !(file << out.str())) throw std::runtime_error("cannot write synthesis Verilog");
    if (!boxes.boxes.empty()) {
        std::ofstream bodies(std::filesystem::path(path).parent_path() / "keep_boxes.v");
        if (!bodies || !(bodies << implementations.str())) throw std::runtime_error("cannot write keep box implementations");
    }
}
}
