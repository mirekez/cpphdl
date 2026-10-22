#include <cpphdl_graph_native.h>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 6 && argc != 7) return 2;
    try {
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("cannot read serialized graph");
        std::ostringstream text;
        text << input.rdbuf();
        cpphdl::graph::Graph graph;
        graph.load(text.str().c_str());
        graph.emit(argv[2], std::stoul(argv[3]), std::stoul(argv[4]), false, false,
                   argc == 7 ? std::stoul(argv[6]) : 0);
        std::ofstream metadata(argv[5]);
        if (!metadata) throw std::runtime_error("cannot write graph memory metadata");
        for (size_t index = 0; index < graph.memories.size(); ++index) {
            const auto& memory = graph.memories[index];
            metadata << index << '\t' << memory.width << '\t' << memory.depth << '\t' << memory.name << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
