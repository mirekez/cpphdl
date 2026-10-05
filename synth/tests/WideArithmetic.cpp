#include "Mapping.h"
#include "Evaluate.h"
#include <iostream>
#include <random>

int main() {
    using namespace cpphdl::graph;
    try {
        std::mt19937_64 random(0x12848);
        for (unsigned width : {65u, 96u, 128u}) {
            Graph graph;
            graph.clockContract = ClockContract::RisingEdgeStep;
            auto al = graph.wire(64, "al", "input"), ah = graph.wire(width - 64, "ah", "input");
            auto bl = graph.wire(64, "bl", "input"), bh = graph.wire(width - 64, "bh", "input");
            graph.ports = {{"al", al, true}, {"ah", ah, true}, {"bl", bl, true}, {"bh", bh, true}};
            auto a = al, b = bl;
            a.insert(a.end(), ah.begin(), ah.end()); b.insert(b.end(), bh.begin(), bh.end());
            auto amount = graph.wire(8, "amount", "input");
            graph.ports.push_back({"amount", amount, true});
            for (const std::string op : {"add", "sub", "mul"}) {
                auto value = graph.binary(op, a, b, width);
                graph.ports.push_back({op + "l", slice(value, 0, 64), false});
                graph.ports.push_back({op + "h", slice(value, 64, width - 64), false});
            }
            for (const std::string op : {"shl", "shr", "sar"}) {
                auto value = graph.binary(op, a, amount, width);
                graph.ports.push_back({op + "l", slice(value, 0, 64), false});
                graph.ports.push_back({op + "h", slice(value, 64, width - 64), false});
            }
            graph.ports.push_back({"narrow", graph.binary("shr", a, amount, 64), false});
            const auto nodes = graph.nodes.size();
            auto scaled = graph.binary("mul", a, constant(8, width), width);
            auto commuted = graph.binary("mul", constant(8, width), a, width);
            if (graph.nodes.size() != nodes || scaled != commuted ||
                slice(scaled, 3, width - 3) != slice(a, 0, width - 3))
                throw std::runtime_error("constant multiply must be wiring before timing");
            auto mapped = cpphdl::synth::mapGates(graph);
            Evaluate words(graph), gates(mapped);
            __uint128_t mask = ~__uint128_t(0) >> (128 - width);
            for (unsigned sample = 0; sample < 128; ++sample) {
                __uint128_t av = ((__uint128_t(random()) << 64) | random()) & mask;
                __uint128_t bv = ((__uint128_t(random()) << 64) | random()) & mask;
                if (sample < 4) { av = sample & 1 ? mask : 0; bv = sample & 2 ? mask : 1; }
                unsigned shift = sample < 5 ? sample * 32 : random() % 256;
                for (auto* sim : {&words, &gates}) {
                    sim->input("al", uint64_t(av)); sim->input("ah", uint64_t(av >> 64));
                    sim->input("bl", uint64_t(bv)); sim->input("bh", uint64_t(bv >> 64));
                    sim->input("amount", shift); sim->eval();
                    for (const std::string op : {"add", "sub", "mul"}) {
                        auto expected = (op == "add" ? av + bv : op == "sub" ? av - bv : av * bv) & mask;
                        auto actual = (__uint128_t(sim->output(op + "h")) << 64) | sim->output(op + "l");
                        if (actual != expected) throw std::runtime_error("wide " + op + " mismatch at width " + std::to_string(width));
                    }
                    for (const std::string op : {"shl", "shr", "sar"}) {
                        __uint128_t expected = shift >= width ? 0 : op == "shl" ? (av << shift) & mask : av >> shift;
                        if (op == "sar" && (av >> (width - 1)) && shift)
                            expected |= shift >= width ? mask : mask ^ (mask >> shift);
                        auto actual = (__uint128_t(sim->output(op + "h")) << 64) | sim->output(op + "l");
                        if (actual != expected) throw std::runtime_error("wide " + op + " mismatch");
                    }
                    if (sim->output("narrow") != (shift >= width ? 0 : uint64_t(av >> shift)))
                        throw std::runtime_error("wide right shift narrowed too early");
                }
            }
        }
        std::cout << "Wide arithmetic, shifts and constant wiring: native graph and mapped gates passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
