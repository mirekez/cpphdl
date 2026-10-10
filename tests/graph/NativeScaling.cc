#include <array>
#include <cstdint>

constexpr unsigned BRANCHES = 12;
constexpr unsigned ROUNDS = SCALING_MEMORY ? 128 : 256;
constexpr uint64_t SEED = 0x9e3779b97f4a7c15ULL;

#ifdef SCALING_EMIT
#include <cpphdl_graph_native.h>
#include <algorithm>
#include <stdexcept>

cpphdl::graph::Graph makeGraph()
{
    using namespace cpphdl::graph;
    Graph graph;
    graph.clockContract = ClockContract::RisingEdgeStep;
    auto data = graph.wire(64, "data", "input");
    auto enable = graph.wire(1, "enable", "input");
    graph.ports = {{"data", data, true}, {"enable", enable, true}};
    if (SCALING_MEMORY) graph.memories.push_back({"ram", 64, 256});
    for (unsigned branch = 0; branch < BRANCHES; ++branch) {
        auto state = graph.wire(64, "state" + std::to_string(branch), "state");
        auto value = graph.binary("xor", state, data, 64);
        for (unsigned round = 0; round < ROUNDS; ++round) {
            if (SCALING_MEMORY) {
                auto address = resize(graph.binary("add", value, constant(round + branch, 64), 64), 8);
                auto read = graph.add("memory_read", 64, address, constant(0, 64), constant(0, 64));
                value = graph.binary("xor", value, read, 64);
            }
            value = graph.binary("add", value, constant(SEED + round * 131 + branch, 64), 64);
            value = graph.binary("xor", value, graph.binary("shr", value, constant(13, 64), 64), 64);
            value = graph.binary("mul", value, constant(0x100000001b3ULL, 64), 64);
        }
        graph.states.push_back({state, value, enable});
        graph.ports.push_back({"out" + std::to_string(branch), state, false});
    }
    if (SCALING_MEMORY)
        graph.memoryWrites.push_back({0, slice(data, 0, 8), data, enable});
    return graph;
}

int main(int argc, char** argv)
{
    using namespace cpphdl::graph;
    if (argc != 2) return 1;
    for (unsigned lanes = 1; lanes <= 2; ++lanes) {
        auto graph = makeGraph();
        auto base = graph.nodes.size();
        auto plan = nativeParallelPlan(graph, graph.dependencyOrder(), lanes);
        if (plan.lanes != lanes) throw std::runtime_error("requested lanes were lost");
        std::vector<unsigned> registers(lanes);
        if (lanes > 1) {
            for (auto owner : plan.states) ++registers.at(owner);
            for (auto count : registers)
                if (count != BRANCHES / lanes) throw std::runtime_error("unbalanced register cones");
            for (const auto& waits : plan.waits)
                if (!waits.empty()) throw std::runtime_error("independent cones acquired cross-lane waits");
            if (graph.nodes.size() != base) throw std::runtime_error("independent work was duplicated");
            auto [smallest, largest] = std::minmax_element(plan.tasks.begin(), plan.tasks.end(),
                [](const auto& left, const auto& right) { return left.size() < right.size(); });
            if (largest->size() > smallest->size() + 4)
                throw std::runtime_error("unbalanced operation counts");
        }
        auto emitted = makeGraph();
        emitted.emit(std::string(argv[1]) + "/model" + std::to_string(lanes) + ".h", 512, lanes);
    }
}
#else
#define cpphdl_native one
#include "model1.h"
#undef cpphdl_native
#define cpphdl_native two
#include "model2.h"
#undef cpphdl_native
#include <chrono>
#include <cstdio>
#include <stdexcept>

static_assert(two::Model::__cpphdl_thread_count == 2);

template<class Model> auto outputs(const Model& model)
{
    return std::array{model.out0, model.out1, model.out2, model.out3, model.out4, model.out5,
                      model.out6, model.out7, model.out8, model.out9, model.out10, model.out11};
}

template<class Model> void initialize(Model& model)
{
#if SCALING_MEMORY
    for (unsigned row = 0; row < 256; ++row) model.memory0[row][0] = SEED * (row + 1);
#endif
}

template<class Model> void check()
{
    Model model;
    initialize(model);
    std::array<uint64_t, BRANCHES> state{};
    std::array<uint64_t, 256> memory{};
    for (unsigned row = 0; row < 256; ++row) memory[row] = SEED * (row + 1);
    for (unsigned sample = 0; sample < 256; ++sample) {
        uint64_t data = SEED * (sample + 1);
        model.data = {uint32_t(data), uint32_t(data >> 32)};
        model.enable[0] = sample % 7 != 0;
        model.eval(false);
        auto observed = outputs(model);
        for (unsigned branch = 0; branch < BRANCHES; ++branch) {
            uint64_t old = uint64_t(observed[branch][0]) | (uint64_t(observed[branch][1]) << 32);
            if (old != state[branch]) throw std::runtime_error("settled output mismatch");
            if (!model.enable[0]) continue;
            auto value = state[branch] ^ data;
            for (unsigned round = 0; round < ROUNDS; ++round) {
                if (SCALING_MEMORY) value ^= memory[(value + round + branch) & 255];
                value += SEED + round * 131 + branch;
                value ^= value >> 13;
                value *= 0x100000001b3ULL;
            }
            state[branch] = value;
        }
        model.eval(true);
        if (outputs(model) != observed) throw std::runtime_error("outputs consumed next state early");
#if SCALING_MEMORY
        if (model.enable[0]) memory[data & 255] = data;
        for (unsigned row = 0; row < 256; ++row)
            if (model.memory0[row][0] != memory[row]) throw std::runtime_error("RAM commit mismatch");
#endif
        model.eval(false);
        observed = outputs(model);
        for (unsigned branch = 0; branch < BRANCHES; ++branch) {
            uint64_t current = uint64_t(observed[branch][0]) | (uint64_t(observed[branch][1]) << 32);
            if (current != state[branch]) throw std::runtime_error("committed output mismatch");
        }
    }
}

template<class Model> double measure(unsigned cycles, uint64_t& checksum)
{
    Model model;
    initialize(model);
    auto start = std::chrono::steady_clock::now();
    for (unsigned cycle = 0; cycle < cycles; ++cycle) {
        uint64_t data = SEED * (cycle + 1);
        model.data = {uint32_t(data), uint32_t(data >> 32)};
        model.enable[0] = 1;
        model.step();
        for (auto port : outputs(model)) checksum = checksum * 131 + port[0] + port[1];
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

int main()
{
    check<one::Model>(); check<two::Model>();
    std::puts("1-2 lanes: balanced private cones, no extra operations, old-state outputs and oracle PASS");
    unsigned cycles = 1024;
    uint64_t checksum = 0;
    while (measure<one::Model>(cycles, checksum) < 0.1 && cycles < 131072) cycles *= 2;
    for (unsigned sample = 0; sample < 5; ++sample) {
        // Rotate measurement order so boost/thermal drift does not favor one lane count.
        for (unsigned position = 0; position < 2; ++position) {
            unsigned lanes = (position + sample) % 2 + 1;
            checksum = 0;
            double seconds = 0;
            if (lanes == 1) seconds = measure<one::Model>(cycles, checksum);
            if (lanes == 2) seconds = measure<two::Model>(cycles, checksum);
            std::printf("measurement %u %u %u %.9f %llu\n", sample, lanes, cycles, seconds,
                        (unsigned long long)checksum);
        }
    }
}
#endif
