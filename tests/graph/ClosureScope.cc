#include "cpphdl_graph.h"
#include <cstdio>
cpphdl::graph::Graph makeGraph();
int main() {
    auto graph = makeGraph();
    graph.optimize();
    unsigned parentAdds = 0, childAdds = 0, childMuls = 0;
    for (auto n : graph.dependencyOrder()) {
        const auto& node = graph.nodes[n];
        parentAdds += node.op == "add" && node.scope == "cpphdl_top";
        childAdds += node.op == "add" && node.scope == "cpphdl_top.multiply";
        childMuls += node.op == "mul" && node.scope == "cpphdl_top.multiply";
    }
    if (parentAdds != 2 || childAdds != 1 || childMuls != 1) {
        std::fprintf(stderr, "closure scope: parent adds=%u child adds=%u child muls=%u\n", parentAdds, childAdds, childMuls);
        return 1;
    }
    std::puts("port binding arithmetic belongs to its defining module, not its consumer");
}
