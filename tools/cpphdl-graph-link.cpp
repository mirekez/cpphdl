// Connect independently lowered C++HDL hierarchy partitions into one graph.
#include <cpphdl_graph_native.h>
#include <iostream>
#include <cstdlib>

using namespace cpphdl::graph;
struct Instance {
    std::map<std::string, Value> ports, enables, resets;
    Value enable{1};
};

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) return 2;
    try {
        // The existing Chipyard build scripts propagate this optimizer setting
        // to the linker. A positional override also supports standalone tools.
        const char* threadArgument = argc == 5 ? argv[4] : std::getenv("CPPHDL_OPTIMIZE_THREADS");
        unsigned threads = 1;
        if (threadArgument) {
            std::string text = threadArgument;
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos ||
                text.size() > 3 || (threads = std::stoul(text)) < 1 || threads > 256)
                throw std::runtime_error("native graph threads must be between 1 and 256");
        }
        std::ifstream plan(argv[1]);
        if (!plan) throw std::runtime_error("cannot read graph link plan");
        Graph graph;
        graph.clockContract = ClockContract::RisingEdgeStep;
        std::map<std::string, Instance> instances;
        std::string command, hostPrefix, hostParent, hostMember;
        unsigned hosts = 0;
        while (plan >> command) {
            if (command == "part") {
                std::string path, filename, parent, member;
                plan >> std::quoted(path) >> std::quoted(filename) >> std::quoted(parent) >> std::quoted(member);
                std::ifstream input(filename);
                if (!input) throw std::runtime_error("cannot read partition: " + filename);
                std::ostringstream text; text << input.rdbuf();
                Graph part; part.load(text.str().c_str());
                if (!part.aliases.empty() || part.clockContract != ClockContract::RisingEdgeStep || !part.pipelines.empty())
                    throw std::runtime_error("partition must be compact with a lifecycle clock contract");
                auto nodeBase = graph.nodes.size(), memoryBase = graph.memories.size();
                auto map = [&](Value value) {
                    for (auto& bit : value) if (bit > 1) bit += nodeBase * 64;
                    return value;
                };
                Instance instance;
                if (!parent.empty()) {
                    auto& owner = instances.at(parent);
                    instance.enable = graph.binary("and", owner.enable, owner.enables.at(member), 1);
                    // The enable expression may allocate a node.
                    nodeBase = graph.nodes.size();
                }
                for (auto node : part.nodes) {
                    node.left = map(std::move(node.left)); node.right = map(std::move(node.right));
                    node.select = map(std::move(node.select));
                    if (node.op == "memory_read") node.right = constant(number(node.right).value() + memoryBase, 64);
                    if (node.hostEffect()) throw std::runtime_error("host effects must be explicit graph link boundaries");
                    node.name = path + "/" + node.name; node.scope = path + "/" + node.scope;
                    graph.nodes.push_back(std::move(node));
                }
                for (size_t i = 0; i < part.nodes.size(); ++i) {
                    auto& node = graph.nodes[nodeBase + i];
                    if (node.validation()) node.left = graph.binary("and", instance.enable, node.left, 1);
                }
                for (auto memory : part.memories) {
                    memory.name = path + "/" + memory.name; graph.memories.push_back(std::move(memory));
                }
                for (auto port : part.ports) instance.ports.emplace(port.name, map(std::move(port.bits)));
                for (auto state : part.states) {
                    const auto& name = part.nodes.at(Graph::owner(state.bits.at(0))).name;
                    auto capture = name.rfind(".__graph_");
                    state.bits = map(std::move(state.bits)); state.next = map(std::move(state.next));
                    state.trigger = map(std::move(state.trigger)); state.reset = map(std::move(state.reset));
                    state.resetValue = map(std::move(state.resetValue));
                    if (capture != std::string::npos) {
                        auto start = name.rfind('.', capture - 1) + 1;
                        auto child = name.substr(start, capture - start);
                        if (name.substr(capture) == ".__graph_enable:0") instance.enables.emplace(child, state.next);
                        else if (name.substr(capture) == ".__graph_reset:0") instance.resets.emplace(child, state.next);
                        else throw std::runtime_error("unknown child lifecycle capture: " + name);
                    } else {
                        state.trigger = graph.binary("and", instance.enable, state.trigger, 1);
                        graph.states.push_back(std::move(state));
                    }
                }
                for (auto access : part.memoryAccesses) {
                    access.memory += memoryBase; access.address = map(std::move(access.address));
                    access.enabled = map(std::move(access.enabled));
                    if (access.transaction) access.enabled = graph.binary("and", instance.enable, access.enabled, 1);
                    graph.memoryAccesses.push_back(std::move(access));
                }
                for (auto write : part.memoryWrites) {
                    write.memory += memoryBase; write.address = map(std::move(write.address));
                    write.data = map(std::move(write.data)); write.enabled = map(std::move(write.enabled));
                    write.enabled = graph.binary("and", instance.enable, write.enabled, 1);
                    graph.memoryWrites.push_back(std::move(write));
                }
                for (const auto& port : part.ports) {
                    auto bits = instance.ports.at(port.name);
                    if (port.name == "work_reset") {
                        graph.connect(bits, parent.empty() ? Value{0} : instances.at(parent).resets.at(member));
                    } else if (port.name.starts_with("r_")) {
                        if (parent.empty()) graph.ports.push_back({port.name, bits, port.input});
                        else {
                            auto peer = instances.at(parent).ports.at("c_" + member + "_" + port.name.substr(2));
                            if (port.input) graph.connect(bits, peer); else graph.connect(peer, bits);
                        }
                    }
                }
                if (!instances.emplace(path, std::move(instance)).second) throw std::runtime_error("duplicate instance: " + path);
                std::cerr << "linked " << path << ": " << part.nodes.size() << " nodes\n";
            } else if (command == "host") {
                std::string path, type;
                plan >> std::quoted(path) >> std::quoted(type) >> std::quoted(hostParent) >> std::quoted(hostMember);
                hostPrefix = "h" + std::to_string(hosts++) + "_";
                auto controlPrefix = "host_control_" + std::to_string(hosts - 1) + "_";
                auto& owner = instances.at(hostParent);
                graph.ports.push_back({controlPrefix + "enable", graph.binary("and", owner.enable, owner.enables.at(hostMember), 1), false});
                graph.ports.push_back({controlPrefix + "work_reset", owner.resets.at(hostMember), false});
            } else if (command == "pin") {
                std::string direction, name; unsigned width;
                plan >> direction >> std::quoted(name) >> width;
                auto peer = instances.at(hostParent).ports.at("c_" + hostMember + "_" + name);
                if (direction == "input") graph.ports.push_back({hostPrefix + name, peer, false});
                else if (direction == "output") {
                    auto bits = graph.wire(width, hostPrefix + name, "input");
                    graph.ports.push_back({hostPrefix + name, bits, true}); graph.connect(peer, bits);
                } else throw std::runtime_error("unknown host pin direction");
            } else throw std::runtime_error("unknown graph link command: " + command);
            if (!plan) throw std::runtime_error("malformed graph link plan");
        }
        std::set<std::string> portNames;
        for (const auto& port : graph.ports)
            if (!portNames.insert(port.name).second) throw std::runtime_error("duplicate linked port: " + port.name);
        graph.optimize(); graph.compact();
        std::ofstream serialized(std::string(argv[2]) + ".graph"); graph.save(serialized);
        unsigned chunkSize = argc >= 4 ? std::stoul(argv[3]) : 512;
        if (!chunkSize) throw std::runtime_error("chunk size must be positive");
        // Reserve a caller lane for host work only when at least two lanes
        // remain for register cones. Keep the existing one/two-thread schedules.
        graph.emit(argv[2], chunkSize, threads, hosts != 0 && threads >= 3);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "graph link: " << error.what() << '\n'; return 1;
    }
}
