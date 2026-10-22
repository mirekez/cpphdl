#include <cpphdl_analog.h>
#include <iostream>

long _system_clock = 0;
using namespace cpphdl::analog;
using namespace analog::literals;

bool analog_first_ports();
bool digital_first_ports();

template <unsigned W> class NativeRegister : public cpphdl::Module {
    cpphdl::reg<cpphdl::logic<W>> state;

public:
    _PORT(cpphdl::logic<W>) D;
    _PORT(cpphdl::logic<W>) Q;
    unsigned edges = 0;

    void _assign() {
        Q = _ASSIGN_REG(state);
    }
    void _work(bool reset) {
        if (reset)
            state._next = 0;
        else {
            ++edges;
            state._next = D();
        }
    }
    void _strobe() {
        state.strobe();
    }
};

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void expect(const char* code, F run) {
    try {
        run();
    } catch (const diagnostic& error) {
        check(error.code == code, "wrong boundary diagnostic");
        return;
    }
    throw std::runtime_error(std::string("missing diagnostic ") + code);
}

int main() {
    unsigned passed = 0;
    auto test = [&](const char* name, auto run) {
        run();
        ++passed;
        std::cout << "PASS " << name << '\n';
    };
    try {
        test("both include orders preserve typed and native ports",
             [] { check(analog_first_ports() && digital_first_ports(), "shared port selector changed native ports"); });
        test("native and analog stores share one old-state snapshot", [] {
            simulation sim;
            bits<8> D, Q1, Q2, Qanalog;
            bits<1> CLK;
            sim.drive(D, 4);
            sim.clock(CLK, 1_us);
            auto first = std::make_shared<NativeRegister<8>>();
            auto second = std::make_shared<NativeRegister<8>>();
            first->D = sim_to_cpphdl(D);
            second->D = sim_to_cpphdl(Q1);
            sim_clock_cpphdl(second, CLK); // Reverse registration order must preserve the pipeline latency.
            sim_clock_cpphdl(first, CLK);
            Q1 = sim_registered_output<8>(first, &NativeRegister<8>::Q);
            Q2 = sim_registered_output<8>(second, &NativeRegister<8>::Q);
            Qanalog = sim_store(Q1, CLK);
            sim.observe("first", Q1);
            sim.observe("second", Q2);
            sim.observe("analog", Qanalog);
            sim.run(2_us, 100_ns);
            const auto& edge = *std::min_element(sim.trace.begin(), sim.trace.end(), [](const auto& a, const auto& b) {
                return std::abs(a.time - .5e-6) < std::abs(b.time - .5e-6);
            });
            check(edge.exact_bits[0] == bit_value(4) && edge.exact_bits[1] == bit_value(0) &&
                      edge.exact_bits[2] == bit_value(0),
                  "a store sampled committed state from the same edge");
            check(Q2.value() == bit_value(4) && Qanalog.value() == bit_value(4), "pipeline failed to advance");
        });
        test("independent native clock domains advance on their own edges", [] {
            simulation sim;
            bits<8> D, Q1, Q2;
            bits<1> fast, slow;
            sim.drive(D, 9);
            sim.clock(fast, 1_us);
            sim.clock(slow, 2_us);
            auto first = std::make_shared<NativeRegister<8>>();
            auto second = std::make_shared<NativeRegister<8>>();
            first->D = sim_to_cpphdl(D);
            second->D = sim_to_cpphdl(Q1);
            sim_clock_cpphdl(first, fast);
            sim_clock_cpphdl(second, slow);
            Q1 = sim_registered_output<8>(first, &NativeRegister<8>::Q);
            Q2 = sim_registered_output<8>(second, &NativeRegister<8>::Q);
            sim.run(2_us, 100_ns);
            check(first->edges == 2 && second->edges == 1, "clock domains were collapsed");
            check(Q2.value() == bit_value(9), "slow domain did not observe the earlier fast edge");
        });
        test("bridge preserves exact 64-bit native values", [] {
            simulation sim;
            bits<64> D, Q;
            bits<1> CLK;
            sim.drive(D, std::numeric_limits<std::uint64_t>::max());
            sim.clock(CLK, 1_us);
            auto module = std::make_shared<NativeRegister<64>>();
            module->D = sim_to_cpphdl(D);
            sim_clock_cpphdl(module, CLK);
            Q = sim_registered_output<64>(module, &NativeRegister<64>::Q);
            sim.observe("wide", Q);
            sim.run(1_us, 100_ns);
            check(Q.value() == bit_value(std::numeric_limits<std::uint64_t>::max()), "native value was rounded");
            check(sim.trace.back().exact_bits[0] == Q.value(), "wide trace lost precision");
        });
        test("simulation retains native model ownership", [] {
            std::weak_ptr<NativeRegister<8>> weak;
            {
                simulation sim;
                bits<8> D, Q;
                bits<1> CLK;
                sim.drive(D, 13);
                sim.clock(CLK, 1_us);
                {
                    auto module = std::make_shared<NativeRegister<8>>();
                    weak = module;
                    module->D = sim_to_cpphdl(D);
                    sim_clock_cpphdl(module, CLK);
                    Q = sim_registered_output<8>(module, &NativeRegister<8>::Q);
                }
                check(!weak.expired(), "native model died before simulation");
                sim.run(1_us, 100_ns);
                check(Q.value() == bit_value(13), "owned native module failed");
            }
            check(weak.expired(), "native bridge leaked a module");
        });
        test("unknown analog interpretation cannot enter native two-state logic", [] {
            expect("E_UNKNOWN_CPPHDL", [] {
                simulation sim;
                electrical A, Vss;
                bits<1> D, Q, CLK;
                sim.reference(Vss);
                A = sim_voltage_source(Vss, .6_V);
                D = sim_sense(A, Vss, .35_V, .85_V);
                sim.clock(CLK, 1_us);
                auto module = std::make_shared<NativeRegister<1>>();
                module->D = sim_to_cpphdl(D);
                sim_clock_cpphdl(module, CLK);
                Q = sim_registered_output<1>(module, &NativeRegister<1>::Q);
                sim.run(1_us, 100_ns);
            });
        });
        test("ambiguous native clock is rejected", [] {
            expect("E_UNKNOWN_CPPHDL", [] {
                simulation sim;
                electrical A, Vss;
                bits<1> D, CLK;
                sim.reference(Vss);
                A = sim_voltage_source(Vss, 0_V, 500_ns, .6_V);
                CLK = sim_sense(A, Vss, .35_V, .85_V);
                sim.drive(D, 1);
                auto module = std::make_shared<NativeRegister<1>>();
                module->D = sim_to_cpphdl(D);
                sim_clock_cpphdl(module, CLK);
                sim.run(1_us, 100_ns);
            });
        });
        test("one native module cannot be registered twice", [] {
            simulation sim;
            bits<1> CLK;
            auto module = std::make_shared<NativeRegister<1>>();
            sim_clock_cpphdl(module, CLK);
            expect("E_DRIVER", [&] { sim_clock_cpphdl(module, CLK); });
        });
        test("bridge rejects cross-simulation inputs and clocks", [] {
            simulation first;
            bits<1> D, CLK;
            simulation second;
            expect("E_CONTEXT", [&] { sim_to_cpphdl(D); });
            auto module = std::make_shared<NativeRegister<1>>();
            expect("E_CONTEXT", [&] { sim_clock_cpphdl(module, CLK); });
        });
        test("native output needs a clocked lifecycle", [] {
            expect("E_UNDERCONSTRAINED", [] {
                simulation sim;
                bits<1> Q;
                auto module = std::make_shared<NativeRegister<1>>();
                module->_assign();
                Q = sim_registered_output<1>(module, &NativeRegister<1>::Q);
                sim.run(1_us, 100_ns);
            });
        });
        test("unbound native output fails without calling an empty lazy port", [] {
            class Unbound : public cpphdl::Module {
            public:
                _PORT(cpphdl::logic<1>) Q;
                void _work(bool) {}
            };
            expect("E_UNDERCONSTRAINED", [] {
                simulation sim;
                bits<1> CLK, Q;
                sim.clock(CLK, 1_us);
                auto module = std::make_shared<Unbound>();
                sim_clock_cpphdl(module, CLK);
                Q = sim_registered_output<1>(module, &Unbound::Q);
                sim.run(1_us, 100_ns);
            });
        });
        test("bridge rejects null models and incompatible output widths", [] {
            simulation sim;
            bits<1> CLK, Q;
            std::shared_ptr<NativeRegister<8>> empty;
            expect("E_VALUE", [&] { sim_clock_cpphdl(empty, CLK); });
            expect("E_VALUE", [&] { sim_registered_output<8>(empty, &NativeRegister<8>::Q); });
            auto module = std::make_shared<NativeRegister<8>>();
            expect("E_TYPE", [&] { Q = sim_registered_output<8>(module, &NativeRegister<8>::Q); });
        });
        std::cout << passed << " C++HDL interoperability checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
