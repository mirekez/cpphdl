#include "cpphdl_graph.h"
#include <cassert>

int main() {
    using namespace cpphdl::graph;
    Graph graph;
    auto state = graph.wire(2, "state", "input");
    auto other = graph.wire(2, "other", "input");
    auto independent = graph.wire(1, "data", "input");
    for (unsigned index = 0; index < 3000; ++index)
        independent = graph.binary("xor", independent, graph.wire(1, "data" + std::to_string(index), "input"), 1);
    auto first = graph.wire(1, "first");
    auto second = graph.wire(1, "second");
    auto third = graph.wire(1, "third");
    graph.connect(first, graph.mux(graph.binary("eq", state, constant(0, 2), 1),
        graph.binary("xor", second, independent, 1),
        graph.mux(graph.binary("eq", other, constant(0, 2), 1),
            graph.binary("xor", third, independent, 1), constant(1, 1))));
    graph.connect(second, graph.mux(graph.binary("eq", state, constant(1, 2), 1), first, constant(1, 1)));
    graph.connect(third, graph.mux(graph.binary("eq", other, constant(1, 2), 1), first, constant(1, 1)));
    graph.ports = {{"first", first, false}, {"second", second, false}, {"third", third, false}};
    auto originalSize = graph.nodes.size();
    assert(!graph.dependencyOrder().empty());
    assert(graph.nodes.size() < originalSize + 1000);
}
