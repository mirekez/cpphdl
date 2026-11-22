#pragma once
#include "MemoryContinuation.h"
#include "Mapping.h"
#include "Evaluate.h"
#include "Verilog.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <string>

using namespace cpphdl::graph;

static bool pattern(uint64_t id, unsigned bit) {
    uint64_t x = (id + 1) * 0x9e3779b97f4a7c15ULL ^
                 (uint64_t(bit) + 1) * 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return (x ^ (x >> 31)) & 1;
}

static const Port& port(const Graph& g, const std::string& name, bool input) {
    for (const auto& p : g.ports)
        if (p.name == name && p.input == input) return p;
    throw std::runtime_error("missing port " + name);
}

static void inputContext(Evaluate& sim, uint64_t id) {
    const auto& bits = port(sim.g, "context_in", true).bits;
    for (unsigned i = 0; i < bits.size(); ++i)
        sim.bits[sim.g.resolve(bits[i])] = pattern(id, i);
}

static void checkContext(Evaluate& sim, uint64_t id) {
    const auto& bits = port(sim.g, "context_out", false).bits;
    for (unsigned i = 0; i < bits.size(); ++i) {
        auto b = sim.g.resolve(bits[i]);
        bool actual = b < 2 ? b : sim.bits[b];
        if (actual != pattern(id, i))
            throw std::runtime_error("context mismatch at bit " + std::to_string(i));
    }
}

struct Entry { uint64_t id; bool issue; };
struct Reply { uint64_t id; unsigned due; };

static void run(unsigned depth, unsigned width, bool fullExchange, bool predicated) {
    Graph g; g.clockContract = ClockContract::RisingEdgeStep;
    std::map<std::string, Value> in;
    for (auto [name, size] : std::map<std::string, unsigned>{
            {"reset", 1}, {"command_valid_in", 1}, {"request_ready_in", 1},
            {"context_in", width}, {"memory_valid_in", 1}, {"memory_data_in", 32},
            {"memory_error_in", 1}, {"response_ready_in", 1}}) {
        in[name] = g.wire(size, name, "input");
        g.ports.push_back({name, in[name], true});
    }
    if (predicated) {
        in["request_enable_in"] = g.wire(1, "request_enable_in", "input");
        g.ports.push_back({"request_enable_in", in.at("request_enable_in"), true});
    }
    auto out = cpphdl::synth::buildMemoryContinuation(g, "continuation", depth, in, fullExchange);
    for (const auto& [name, value] : out) g.ports.push_back({name, value, false});
    if (depth == 2) {
        if (!g.memories.empty()) throw std::runtime_error("depth2 kept a RAM");
    } else if (g.memories.size() != 1 || g.memories[0].depth != depth ||
               g.memories[0].width != width) {
        throw std::runtime_error("deep context RAM changed width or depth");
    }
    bool sidecar = false;
    for (const auto& node : g.nodes)
        if (node.name.find("request_enables") != std::string::npos) sidecar = true;
    if (depth == 8 && predicated && !sidecar)
        throw std::runtime_error("missing deep request enable sidecar");
    (void)cpphdl::synth::verilogText(g, "MemoryBoundaryProbe");
    Evaluate sim(cpphdl::synth::mapGates(g));
    std::deque<Entry> queue;
    std::deque<Reply> replies;
    unsigned sent = 0, received = 0, cancelled = 0, skipped = 0;
    unsigned maxQueue = 0, fullExchangeCount = 0, lastExchangeCount = 0;
    bool resetWithOutstanding = false;
    for (unsigned clock = 0; clock < 1000; ++clock) {
        bool reset = clock < 3 || (clock >= 80 && clock < 83);
        bool commandValid = clock < 80 || (clock >= 83 && clock < 200) ||
            (clock >= 200 && clock < 260 && queue.size() == 1 &&
             (!queue.front().issue || (!replies.empty() && clock >= replies.front().due))) ||
            (clock >= 260 && clock < 450 && clock % 5 != 0);
        bool issue = !predicated || sent % 4 != 1;
        bool lastExchangeWindow = clock >= 200 && clock < 260 && queue.size() == 1;
        bool requestReady = lastExchangeWindow || clock < 30 || clock % 7 != 0;
        bool responseReady = lastExchangeWindow || (clock >= 30 && clock % 11 < 8);
        bool memoryValid = !replies.empty() && clock >= replies.front().due;
        sim.input("reset", reset);
        sim.input("command_valid_in", commandValid);
        sim.input("request_ready_in", requestReady);
        sim.input("response_ready_in", responseReady);
        sim.input("memory_valid_in", memoryValid);
        sim.input("memory_data_in", memoryValid ? replies.front().id * 3 : 0);
        sim.input("memory_error_in", memoryValid && replies.front().id % 13 == 0);
        if (predicated) sim.input("request_enable_in", issue);
        inputContext(sim, sent);
        sim.eval();
        bool available = !reset && !queue.empty();
        bool responseValid = available && (!queue.front().issue || memoryValid);
        bool memoryReady = available && queue.front().issue && responseReady;
        bool pop = responseValid && responseReady;
        bool space = queue.size() < depth || (fullExchange && pop);
        bool commandReady = !reset && space && (!issue || requestReady);
        bool requestValid = !reset && space && issue && commandValid;
        bool push = commandValid && commandReady;
        if (sim.output("response_valid_out") != responseValid ||
            sim.output("memory_ready_out") != memoryReady ||
            sim.output("command_ready_out") != commandReady ||
            sim.output("request_valid_out") != requestValid)
            throw std::runtime_error("protocol mismatch at clock " + std::to_string(clock));
        if (responseValid) {
            checkContext(sim, queue.front().id);
            uint64_t data = queue.front().issue ? replies.front().id * 3 : 0;
            bool error = queue.front().issue && replies.front().id % 13 == 0;
            if (sim.output("data_out") != data || sim.output("error_out") != error)
                throw std::runtime_error("reply data/error mismatch");
        }
        if (queue.size() == depth && pop && push) ++fullExchangeCount;
        if (queue.size() == 1 && pop && push) ++lastExchangeCount;
        if (reset && clock >= 80 && !queue.empty()) resetWithOutstanding = true;
        sim.tick();
        if (reset) {
            cancelled += queue.size(); queue.clear(); replies.clear();
        } else {
            if (pop) {
                if (queue.front().issue) replies.pop_front();
                queue.pop_front(); ++received;
            }
            if (push) {
                queue.push_back({sent, issue});
                if (issue) replies.push_back({sent, clock + 8 + sent % 3});
                else ++skipped;
                ++sent;
            }
        }
        maxQueue = std::max(maxQueue, unsigned(queue.size()));
    }
    if (!queue.empty() || !replies.empty() || sent < 40 || received < 40 ||
        maxQueue != depth || !resetWithOutstanding || !lastExchangeCount ||
        (predicated && !skipped) ||
        (fullExchange && !fullExchangeCount) || (!fullExchange && fullExchangeCount))
        throw std::runtime_error("missing coverage depth=" + std::to_string(depth) +
            " width=" + std::to_string(width) + " fullExchange=" +
            std::to_string(fullExchange) + " predicated=" + std::to_string(predicated) +
            " sent=" + std::to_string(sent) + " received=" + std::to_string(received) +
            " maxQueue=" + std::to_string(maxQueue) + " resetOutstanding=" +
            std::to_string(resetWithOutstanding) + " full=" +
            std::to_string(fullExchangeCount) + " last=" +
            std::to_string(lastExchangeCount) + " skips=" + std::to_string(skipped));
    std::printf("PASS depth=%u width=%u fullExchange=%u predicated=%u sent=%u received=%u cancelled=%u full=%u last=%u skips=%u\n",
        depth, width, fullExchange, predicated, sent, received, cancelled,
        fullExchangeCount, lastExchangeCount, skipped);
}

inline void runFocusedMemoryBoundaryChecks() {
    for (unsigned depth : {2u, 8u})
        for (unsigned width : {depth == 2 ? 513u : 97u, depth == 2 ? 553u : 98u})
            for (bool exchange : {false, true})
                for (bool predicated : {false, true})
                    run(depth, width, exchange, predicated);
}
