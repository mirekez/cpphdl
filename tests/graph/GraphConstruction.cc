#include "cpphdl_graph.h"
#include <iostream>

using namespace cpphdl::graph;

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Graph graph;
    const std::string mode = argv[1];
    if (mode == "storage") {
        const auto count = std::stoull(argv[2]);
        for (size_t index = 0; index < count; ++index) {
            auto bit = graph.add("input", 1);
            if (Graph::owner(bit[0]) != index) return 1;
        }
        if (graph.nodes.size() != count) return 1;
    } else if (mode == "folds") {
        const auto count = std::stoull(argv[2]);
        auto input = graph.add("input", 64);
        auto enable = graph.add("input", 1);
        auto data = input;
        for (size_t index = 0; index < count; ++index) {
            auto maskBits = constant(uint64_t(1) << (index % 64), 64);
            auto field = graph.binary("and", data, maskBits, 64);
            auto rest = graph.binary("and", data, graph.unary("not", maskBits), 64);
            auto merged = graph.binary("or", rest, field, 64);
            auto selected = graph.mux(enable, merged, data);
            data = graph.binary("or", selected, constant(0, 64), 64);
            if (graph.resolved(data) != input) return 1;
        }
        if (graph.nodes.size() != 2 || !graph.aliases.empty()) return 1;
    } else if (mode == "emit") {
        auto data = graph.wire(64, "data_in", "input");
        auto other = graph.wire(64, "other_in", "input");
        auto enable = graph.wire(1, "enable_in", "input");
        graph.ports = {{"data_in", data, true}, {"other_in", other, true}, {"enable_in", enable, true}};
        auto delayed = graph.wire(64, "delayed");
        auto selected = graph.mux(enable, delayed, other);
        auto masked = graph.binary("and", selected, constant(0x00ff00ff00ff00ffull, 64), 64);
        auto shifted = graph.binary("shr", data, constant(7, 32), 64);
        auto result = graph.binary("xor", masked, shifted, 64);
        graph.connect(delayed, data);
        graph.ports.push_back({"result_out", result, false});
        auto wide = data;
        wide.insert(wide.end(), other.begin(), other.end());
        wide.push_back(enable[0]);
        auto complemented = graph.unary("not", wide);
        auto folded = graph.binary("xor", complemented, wide, wide.size());
        graph.ports.push_back({"wide_out", graph.unary("all", folded), false});
        auto carry = graph.binary("add", data, other, 64);
        graph.ports.push_back({"sum_out", carry, false});
        graph.writeCpp(argv[2]);
    } else return 2;
    std::cout << mode << ": nodes=" << graph.nodes.size() << " aliases=" << graph.aliases.size() << '\n';
}
