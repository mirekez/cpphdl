#include <analog.hpp>
#include <basic/basic.hpp>
#include <iostream>
#include <type_traits>

using namespace analog;
using namespace analog::literals;

// These are C++ type-system checks, not a separate DSL's runtime unit checker.
static_assert(!std::is_constructible_v<resistor, const electrical&, capacitance_value>);
static_assert(!std::is_constructible_v<capacitor, const electrical&, resistance>);
static_assert(!std::is_copy_constructible_v<pmos>);
static_assert(std::is_abstract_v<IC>);
static_assert(std::is_base_of_v<IC, cmos<>>);
static_assert(!std::is_copy_constructible_v<cmos<>>);
static_assert(!std::is_convertible_v<cmos<>&, const electrical&>);
static_assert(std::is_same_v<decltype(IC::Vdd), port<electrical>>);
static_assert(std::is_same_v<decltype(IC::Vss), port<electrical>>);
static_assert(std::is_base_of_v<IC, regulator>);
static_assert(std::is_base_of_v<IC, opamp>);
static_assert(std::is_base_of_v<IC, level_inverter>);
static_assert(!std::is_convertible_v<level_inverter&, const electrical&>);

class InverterIC : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;

    void _assign() override {
        auto pmos_vdd = pmos::from(Vdd);
        Y = pmos_vdd(A);
        Vss = nmos(A, Y);
    }
};

class TwoStageIC : public IC {
    cmos<> first, second;

public:
    _PORT(electrical) A;
    _PORT(electrical) Y;

    void _assign() override {
        first.A = A;
        first.Vdd = Vdd;
        first.Vss = Vss;
        first.elaborate();
        first.Y = capacitor(Vss, 10_pF);
        second.A = first.Y;
        second.Vdd = Vdd;
        second.Vss = Vss;
        second.elaborate();
        Y = second.Y;
    }
};

class RegisterIC : public IC {
public:
    _PORT(bits<8>) D;
    _PORT(bits<1>) CLK;
    _PORT(bits<8>) Q;

    void _assign() override {
        Q = sim_store(D, CLK);
    }
};

template <class Component>
void bind_inverter(Component& component, const electrical& A, const electrical& Vdd, const electrical& Vss) {
    component.A = A;
    component.Vdd = Vdd;
    component.Vss = Vss;
    component.elaborate();
}

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = 1e-7) {
    check(std::abs(a - b) <= tolerance, "numerical expectation failed");
}
template <class F> void expect(const std::string& code, F function) {
    try {
        function();
    } catch (const diagnostic& error) {
        check(error.code == code, "wrong diagnostic");
        return;
    }
    throw std::runtime_error("missing expected diagnostic " + code);
}
double rc(duration step) {
    simulation sim;
    electrical IN, Vss;
    sim.reference(Vss);
    IN = sim_voltage_source(Vss, 1_V);
    capacitance OUT(1_nF, Vss);
    OUT = resistor(IN, 1_kOhm);
    sim.run(5_us, step);
    return sim.voltage(OUT, Vss).value;
}

int main() {
    unsigned passed = 0;
    auto test = [&](const char* name, auto run) {
        run();
        ++passed;
        std::cout << "PASS " << name << '\n';
    };
    try {
        test("IC identity and member ports survive construction handles", [] {
            simulation sim;
            electrical A, Vdd, Vss, output;
            sim.reference(Vss);
            A = sim_voltage_source(Vss, 0_V);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            {
                InverterIC gate;
                IC& component = gate;
                check(&gate.Vdd == &component.Vdd && &gate.Vss == &component.Vss,
                      "derived IC shadows the inherited supply ports");
                component.name("inverter");
                check(&component.owner() == &sim && component.id() == 0, "wrong IC identity/context");
                bind_inverter(gate, A, Vdd, Vss);
                auto count = sim.elements.size();
                component.elaborate();
                check(sim.elements.size() == count, "repeated elaboration duplicated models");
                check(sim.elements.back().component == component.id(), "device lost its IC provenance");
                output = gate.Y;
            }
            output = capacitor(Vss, 10_pF); // Give the channel-only model an explicit startup/storage state.
            sim.run(1_us, 100_ns);
            near(sim.voltage(output, Vss).value, 1.2);
            check(sim.components().size() == 1 && sim.components()[0].name == "inverter", "IC metadata lifetime");
        });
        test("inherited supply ports preserve independent power domains", [] {
            simulation sim;
            electrical A, rail_low, rail_high, Vss;
            sim.reference(Vss);
            A = sim_voltage_source(Vss, 0_V);
            rail_low = sim_voltage_source(Vss, 1.2_V);
            rail_high = sim_voltage_source(Vss, 1.8_V);
            cmos<fast_model> first, second;
            check(first.Vdd.id() != second.Vdd.id(), "IC instances share an implicit global rail");
            bind_inverter(first, A, rail_low, Vss);
            bind_inverter(second, A, rail_high, Vss);
            sim.run(1_us, 100_ns);
            near(sim.voltage(first.Y, Vss).value, 1.2);
            near(sim.voltage(second.Y, Vss).value, 1.8);
            check(sim.components().size() == 4, "fast-model child ICs are missing");
        });
        test("unused inherited rails have no fabricated voltage", [] {
            simulation sim;
            RegisterIC component;
            sim.drive(component.D, 3);
            sim.clock(component.CLK, 1_us);
            component.elaborate();
            sim.run(1_us, 100_ns);
            check(component.Q.value() == bit_value(3), "unused rails broke a logical model");
            expect("E_UNVERIFIED", [&] { sim.voltage(component.Vdd, component.Vss); });
        });
        test("observing an unused supply requires a physical solution", [] {
            expect("E_SINGULAR", [] {
                simulation sim;
                RegisterIC component;
                sim.reference(component.Vss);
                sim.drive(component.D, 3);
                sim.clock(component.CLK, 1_us);
                component.elaborate();
                sim.observe("unpowered", component.Vdd, component.Vss);
                sim.run(1_us, 100_ns);
            });
        });
        test("a rail used by a physical model cannot be dropped as unused", [] {
            expect("E_SINGULAR", [] {
                simulation sim;
                cmos<fast_model> gate;
                sim.reference(gate.Vss);
                gate.A = sim_voltage_source(gate.Vss, 0_V);
                // Vdd is deliberately unpowered; inheritance must not supply it.
                gate.elaborate();
                sim.run(1_us, 100_ns);
            });
        });
        test("IC declarations must be elaborated before integration", [] {
            expect("E_ELABORATION", [] {
                simulation sim;
                InverterIC gate;
                sim.run(1_us, 100_ns);
            });
        });
        test("nested IC member ports retain one coupled physical network", [] {
            simulation sim;
            electrical A, Vdd, Vss;
            sim.reference(Vss);
            A = sim_voltage_source(Vss, 0_V, 2_us, 1.2_V);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            TwoStageIC pair;
            bind_inverter(pair, A, Vdd, Vss);
            pair.Y = capacitor(Vss, 10_pF);
            sim.run(4_us, 10_ns);
            check(sim.components().size() == 3, "missing parent/child IC identities");
            near(sim.voltage(pair.Y, Vss).value, 1.2, 1e-6);
            for (const auto& component : sim.components())
                check(component.state == component_state::ready, "nested IC not elaborated");
        });
        test("recursive IC declaration is rejected and cannot run", [] {
            class RecursiveIC : public IC {
            public:
                void _assign() override {
                    elaborate();
                }
            };
            simulation sim;
            RecursiveIC component;
            expect("E_ELABORATION", [&] { component.elaborate(); });
            expect("E_ELABORATION", [&] { sim.run(1_us, 100_ns); });
        });
        test("member bit ports preserve simultaneous register snapshots", [] {
            simulation sim;
            bits<8> D;
            bits<1> CLK;
            sim.drive(D, 4);
            sim.clock(CLK, 1_us);
            RegisterIC first, second;
            first.D = D;
            first.CLK = CLK;
            first.elaborate();
            second.D = first.Q;
            second.CLK = CLK;
            second.elaborate();
            sim.observe("Q", first.Q);
            sim.observe("R", second.Q);
            sim.run(2_us, 100_ns);
            const auto& row = *std::min_element(sim.trace.begin(), sim.trace.end(), [](auto& a, auto& b) {
                return std::abs(a.time - .5e-6) < std::abs(b.time - .5e-6);
            });
            check(row.exact_bits[0] == bit_value(4) && row.exact_bits[1] == bit_value(0), "port wiring lost a cycle");
            check(second.Q.value() == bit_value(4), "second register never captured");
        });
        test("two purposes retain wire identity and both devices", [] {
            simulation sim;
            electrical A, Y, Vdd, Vss;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            A = sim_voltage_source(Vss, 0_V, 2_us, 1.2_V);
            auto id = Y.id();
            auto pmos_vdd = pmos::from(Vdd);
            Y = pmos_vdd(A);
            Vss = nmos(A, Y);
            Y = capacitor(Vss, 10_pF);
            check(Y.id() == id, "purpose replaced wire identity");
            check(sim.elements.size() == 5, "purpose erased or duplicated a device");
            sim.observe("Y", Y, Vss);
            sim.run(4_us, 10_ns);
            near(*sim.trace[100].values[0], 1.2, 1e-6);
            near(sim.voltage(Y, Vss).value, 0, 1e-6);
        });
        test("active capacitance type equals separately connected storage", [] {
            simulation sim;
            electrical A, Y, Vdd, Vss;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            A = sim_voltage_source(Vss, 0_V, 2_us, 1.2_V);
            capacitance active(10_pF, Vss);
            auto pmos_vdd = pmos::from(Vdd);
            Y = pmos_vdd(A);
            Vss = nmos(A, Y);
            Y = capacitor(Vss, 10_pF);
            active = pmos_vdd(A);
            Vss = nmos(A, active);
            sim.observe("wire", Y, Vss);
            sim.observe("active", active, Vss);
            sim.run(4_us, 10_ns);
            for (auto& row : sim.trace)
                near(*row.values[0], *row.values[1]);
        });
        test("RC matches backward Euler independently", [] { near(rc(50_ns), 1 - std::pow(1 / 1.05, 100), 1e-8); });
        test("step refinement approaches analytic physics", [] {
            double exact = 1 - std::exp(-5);
            check(std::abs(rc(25_ns) - exact) < .55 * std::abs(rc(50_ns) - exact), "no first-order refinement");
        });
        test("feedback follows the differential pair and loaded divider", [] {
            simulation sim;
            electrical IN, OUT, Vdd, Vss;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 3.3_V);
            IN = sim_voltage_source(Vss, .5_V);
            non_inverting_amplifier amp;
            amp.IN = IN;
            amp.OUT = OUT;
            amp.Vdd = Vdd;
            amp.Vss = Vss;
            amp.elaborate();
            Vss = resistor(OUT, 100_kOhm);
            sim.run(40_us, 100_ns);
            double output = sim.voltage(OUT, Vss).value;
            near(sim.voltage(amp.feedback.TAP, Vss).value, output / 2);
            // Independently solve input-pair bias and output KCL in the all-saturated region.
            auto balance = [](double out) {
                double low = 0, high = std::min(.5, out / 2) - .35;
                for (int i = 0; i < 80; ++i) {
                    double tail = (low + high) / 2;
                    double left = .1 * std::pow(.5 - tail - .35, 2);
                    double right = .1 * std::pow(out / 2 - tail - .35, 2);
                    if (left + right > tail / 10)
                        low = tail;
                    else
                        high = tail;
                }
                double tail = (low + high) / 2;
                return .1 * (std::pow(.5 - tail - .35, 2) - std::pow(out / 2 - tail - .35, 2)) -
                       out * (1.0 / 20000 + 1.0 / 100000 + 1.0 / 100000);
            };
            double low = .9, high = 1.0;
            for (int i = 0; i < 80; ++i) {
                double midpoint = (low + high) / 2;
                if (balance(midpoint) > 0)
                    low = midpoint;
                else
                    high = midpoint;
            }
            // The independent ideal-mirror estimate omits less than 3.3 nA of bias-return current.
            near(output, (low + high) / 2, 1e-6);
            check(output > .99 && output < 1, "gain-two precision goal failed");
        });
        test("basic divider remains load dependent", [] {
            simulation sim;
            electrical Vdd, Vss;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 3.3_V);
            resistor_divider divider;
            divider.TOP = Vdd;
            divider.BOTTOM = Vss;
            divider.elaborate();
            Vss = resistor(divider.TAP, 10_kOhm);
            sim.run(1_us, 100_ns);
            near(sim.voltage(divider.TAP, Vss).value, 1.1);
        });
        test("basic RC schema agrees with an independently declared circuit", [] {
            double expected = rc(50_ns);
            simulation sim;
            electrical IN, Vss;
            sim.reference(Vss);
            IN = sim_voltage_source(Vss, 1_V);
            rc_filter filter;
            filter.IN = IN;
            filter.Vss = Vss;
            filter.elaborate();
            sim.run(5_us, 50_ns);
            near(sim.voltage(filter.OUT, Vss).value, expected);
        });
        test("transistor amplifier has finite drive under heavy loading", [] {
            auto run = [](resistance load) {
                simulation sim;
                electrical IN, Vdd, Vss;
                sim.reference(Vss);
                IN = sim_voltage_source(Vss, .5_V);
                Vdd = sim_voltage_source(Vss, 3.3_V);
                non_inverting_amplifier amp;
                amp.IN = IN;
                amp.Vdd = Vdd;
                amp.Vss = Vss;
                amp.elaborate();
                Vss = resistor(amp.OUT, load);
                sim.run(40_us, 100_ns);
                return sim.voltage(amp.OUT, Vss).value;
            };
            double light = run(100_kOhm), heavy = run(1_kOhm);
            check(light > .99 && heavy < light - .05, "external load cannot influence the op-amp");
        });
        test("composed regulator follows amplifier and transistor equilibrium", [] {
            simulation sim;
            electrical VIN, REF, Vss;
            sim.reference(Vss);
            VIN = sim_voltage_source(Vss, 3.3_V);
            REF = sim_voltage_source(Vss, 1.2_V);
            capacitance OUT(100_nF, Vss);
            regulator supply;
            supply.Vdd = VIN;
            supply.Vss = Vss;
            supply.REF = REF;
            supply.OUT = OUT;
            supply.elaborate();
            Vss = resistor(OUT, 100_Ohm);
            sim.run(1_ms, 500_ns);
            double output = sim.voltage(OUT, Vss).value;
            double gate = sim.voltage(supply.CONTROL, Vss).value;
            check(output > 1.2 && output < 1.201, "transistor error amplifier did not regulate");
            near(gate, 3.3 - .35 - std::sqrt(2 * output / (100 * .02)), 2e-6);
            near(-channel_current(gate, output, 3.3, .02, .35, true), output / 100, 1e-8);
            check(output - 1.2 > 1e-6, "reference forced the output instead of participating in feedback");
            check(sim.elements.size() == 13, "regulator contains a hidden device law");
            check(sim.components().size() == 4, "missing regulator/amplifier/pair/mirror hierarchy");
        });
        test("composed regulator loses regulation when pass current is insufficient", [] {
            simulation sim;
            electrical VIN, REF, Vss;
            sim.reference(Vss);
            VIN = sim_voltage_source(Vss, 3.3_V);
            REF = sim_voltage_source(Vss, 1.2_V);
            capacitance OUT(100_nF, Vss);
            regulator supply;
            supply.Vdd = VIN;
            supply.Vss = Vss;
            supply.REF = REF;
            supply.OUT = OUT;
            supply.elaborate();
            Vss = resistor(OUT, 1_Ohm); // The requested current exceeds the pass device's capability.
            sim.run(1_ms, 500_ns);
            check(sim.voltage(OUT, Vss).value < .1, "overload did not defeat the regulator purpose");
        });
        test("regulator pass current loads its supply wire", [] {
            simulation sim;
            electrical IDEAL, VIN, REF, Vss;
            sim.reference(Vss);
            IDEAL = sim_voltage_source(Vss, 3.3_V);
            VIN = resistor(IDEAL, 10_Ohm);
            REF = sim_voltage_source(Vss, 1.2_V);
            capacitance OUT(100_nF, Vss);
            regulator supply;
            supply.Vdd = VIN;
            supply.Vss = Vss;
            supply.REF = REF;
            supply.OUT = OUT;
            supply.elaborate();
            Vss = resistor(OUT, 100_Ohm);
            sim.run(1_ms, 50_ns); // Resolve the coupled supply/bias startup transient.
            double output = sim.voltage(OUT, Vss).value;
            double tail_current = sim.voltage(supply.error.TAIL, Vss).value / 10;
            double shunt_current = sim.voltage(supply.CONTROL, Vss).value / 100000;
            near((3.3 - sim.voltage(VIN, Vss).value) / 10, output / 100 + tail_current + shunt_current, 1e-8);
            check(tail_current > .01, "amplifier supply consumption is missing");
        });
        test("shared rail transfers a load influence to a second IC", [] {
            simulation sim;
            electrical ideal, A, Vdd, Vss;
            sim.reference(Vss);
            ideal = sim_voltage_source(Vss, 1.2_V);
            Vdd = resistor(ideal, 200_Ohm);
            A = sim_voltage_source(Vss, 0_V);
            cmos<> loaded, neighbor;
            bind_inverter(loaded, A, Vdd, Vss);
            bind_inverter(neighbor, A, Vdd, Vss);
            loaded.Y = capacitor(Vss, 10_pF, 1.2_V);
            neighbor.Y = capacitor(Vss, 10_pF, 1.2_V);
            bits<1> select;
            sim.step(select, 1_us, 1, 0);
            loaded.Y = sim_driver(select, Vdd, Vss, 100_Ohm);
            sim.require_final_voltage("loaded high", loaded.Y, Vss, 1_V, 1.3_V);
            sim.require_final_voltage("neighbor high", neighbor.Y, Vss, 1_V, 1.3_V);
            sim.run(2_us, 5_ns);
            double y = sim.voltage(loaded.Y, Vss).value, rail = sim.voltage(Vdd, Vss).value;
            check(y < .04 && rail < 1.15, "load failed to influence network");
            near((1.2 - rail) / 200, y / 100, 1e-9);
            near(sim.voltage(neighbor.Y, Vss).value, rail);
            check(sim.status() == "failed", "purpose failure not reported");
            check(sim.obligations()[1].status == "sampled_pass", "neighbor should still satisfy high goal");
        });
        test("register purposes commit simultaneously", [] {
            simulation sim;
            bits<8> A, B, SUM, Q, R;
            bits<1> CLK;
            sim.drive(A, 250);
            sim.drive(B, 10);
            sim.clock(CLK, 1_us);
            SUM = sim_add(A, B);
            Q = sim_store(SUM, CLK);
            R = sim_store(Q, CLK);
            sim.observe("Q", Q);
            sim.observe("R", R);
            sim.run(2_us, 100_ns);
            auto row = *std::min_element(sim.trace.begin(), sim.trace.end(), [](auto& a, auto& b) {
                return std::abs(a.time - .5e-6) < std::abs(b.time - .5e-6);
            });
            check(row.exact_bits[0] == bit_value(4) && row.exact_bits[1] == bit_value(0),
                  "register cascade lost one cycle");
            check(Q.value() == bit_value(4) && R.value() == bit_value(4), "final pipeline values");
        });
        test("source breakpoint preserves previous capacitor charge", [] {
            simulation sim;
            electrical IN, Vss;
            sim.reference(Vss);
            IN = sim_voltage_source(Vss, 0_V, 300_ns, 1_V);
            capacitance OUT(1_nF, Vss);
            OUT = resistor(IN, 1_kOhm);
            sim.observe("IN", IN, Vss);
            sim.observe("OUT", OUT, Vss);
            sim.run(1_us, 100_ns);
            auto row = *std::min_element(sim.trace.begin(), sim.trace.end(), [](auto& a, auto& b) {
                return std::abs(a.time - 300e-9) < std::abs(b.time - 300e-9);
            });
            near(*row.values[0], 1);
            near(*row.values[1], 0);
        });
        test("explicit threshold storage and physical drive compose", [] {
            simulation sim;
            electrical A, Vdd, Vss, Y;
            bits<1> D, Q, CLK;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            A = sim_voltage_source(Vss, 0_V, 750_ns, 1.2_V);
            D = sim_sense(A, Vss, .35_V, .85_V);
            Q = sim_store(D, CLK);
            Y = sim_driver(Q, Vdd, Vss, 100_Ohm);
            Y = capacitor(Vss, 1_nF);
            Vss = resistor(Y, 10_kOhm);
            sim.clock(CLK, 1_us);
            sim.run(3_us, 25_ns);
            check(Q.value() == bit_value(1), "captured wrong logic");
            near(sim.voltage(Y, Vss).value, 1.2 * 10000 / 10100, 3e-6);
        });
        test("unknown interpretation remains unverified", [] {
            simulation sim;
            electrical A, Vss;
            bits<1> D;
            sim.reference(Vss);
            A = sim_voltage_source(Vss, .6_V);
            D = sim_sense(A, Vss, .35_V, .85_V);
            sim.require_final("known high", D, 1);
            sim.run(1_us, 100_ns);
            check(!D.value() && sim.status() == "unverified", "unknown invented a logic value");
        });
        test("ambiguous clock cannot promise retention", [] {
            simulation sim;
            electrical A, Vss;
            bits<1> D, Q, CLK;
            sim.reference(Vss);
            A = sim_voltage_source(Vss, 0_V, 500_ns, .6_V);
            CLK = sim_sense(A, Vss, .35_V, .85_V);
            sim.drive(D, 1);
            Q = sim_store(D, CLK);
            sim.run(1_us, 100_ns);
            check(!Q.value(), "uncertain clock edge was treated as definitely absent");
        });
        test("floating zero residual still checks equation rank", [] {
            expect("E_SINGULAR", [] {
                simulation sim;
                electrical A, B, Vss;
                sim.reference(Vss);
                B = resistor(A, 1_kOhm);
                sim.run(1_us, 100_ns);
            });
        });
        test("digital purpose enforces single writer", [] {
            expect("E_DRIVER", [] {
                simulation sim;
                bits<1> A;
                sim.drive(A, 0);
                sim.drive(A, 1);
            });
        });
        test("wires cannot cross unrelated simulations", [] {
            expect("E_CONTEXT", [] {
                simulation one;
                electrical A;
                simulation two;
                electrical B;
                A = B;
            });
        });
        test("negative resistance rejected", [] {
            expect("E_VALUE", [] {
                simulation sim;
                electrical A, Y;
                Y = resistor(A, resistance(-1));
            });
        });
        test("physical drive cannot choose an unknown rail", [] {
            expect("E_UNKNOWN_PHYSICAL", [] {
                simulation sim;
                electrical A, Vdd, Vss, Y;
                bits<1> D;
                sim.reference(Vss);
                A = sim_voltage_source(Vss, .6_V);
                Vdd = sim_voltage_source(Vss, 1.2_V);
                D = sim_sense(A, Vss, .35_V, .85_V);
                Y = sim_driver(D, Vdd, Vss, 100_Ohm);
                sim.run(1_us, 100_ns);
            });
        });
        test("violated assumptions cannot become passed requirements", [] {
            simulation sim;
            electrical Y, Vss;
            sim.reference(Vss);
            Y = sim_voltage_source(Vss, 1_V);
            sim.assume_voltage("envelope", Y, Vss, 1.1_V, 1.3_V);
            sim.require_final_voltage("bound", Y, Vss, 0_V, 1.3_V);
            sim.run(1_us, 100_ns);
            check(sim.status() == "out_of_contract", "assumption silently enforced or ignored");
        });
        test("64-bit values preserve exact arithmetic and trace", [] {
            simulation sim;
            bits<64> A, B, S;
            sim.drive(A, std::numeric_limits<std::uint64_t>::max());
            sim.drive(B, 1);
            S = sim_add(A, B);
            sim.observe("A", A);
            sim.observe("S", S);
            sim.require_final("wrap", S, 0);
            sim.run(1_us, 100_ns);
            check(S.value() == bit_value(0), "wide modulo arithmetic");
            check(sim.trace[0].exact_bits[0] == std::numeric_limits<std::uint64_t>::max(), "trace rounded wide bits");
        });
        test("level inverter composes complementary paths with finite drive", [] {
            for (const auto& [input, expected] : std::vector<std::pair<voltage, voltage>>{{.1_V, .6_V},
                                                                                          {.35_V, .6_V},
                                                                                          {.475_V, .45_V},
                                                                                          {.6_V, .3_V},
                                                                                          {.725_V, .15_V},
                                                                                          {.85_V, 0_V},
                                                                                          {1.1_V, 0_V}}) {
                simulation sim;
                electrical Vdd, Vss, A;
                sim.reference(Vss);
                Vdd = sim_voltage_source(Vss, 1.2_V);
                A = sim_voltage_source(Vss, input);
                level_inverter gate;
                gate.A = A;
                gate.Vdd = Vdd;
                gate.Vss = Vss;
                gate.elaborate();
                Vss = resistor(gate.Y, 2_kOhm);
                sim.run(1_us, 100_ns);
                // Divider equilibrium: equal external/on resistance halves the unloaded output.
                near(sim.voltage(gate.Y, Vss).value, expected.value);
                auto paths = std::count_if(sim.elements.begin(), sim.elements.end(),
                                           [](const auto& e) { return e.kind == law::sim_controlled_resistor; });
                check(paths == 2, "level inverter did not expand into two controlled paths");
            }
        });
        test("level inverter input interpretation follows its Vss port", [] {
            simulation sim;
            electrical Vdd, Vss, A, reference;
            sim.reference(reference);
            Vdd = sim_voltage_source(reference, 2.2_V);
            Vss = sim_voltage_source(reference, 1_V);
            A = sim_voltage_source(reference, 1.6_V); // 0.6 V relative to the IC's Vss.
            level_inverter gate;
            gate.A = A;
            gate.Vdd = Vdd;
            gate.Vss = Vss;
            gate.elaborate();
            Vss = resistor(gate.Y, 2_kOhm);
            sim.run(1_us, 100_ns);
            near(sim.voltage(gate.Y, reference).value, 1.3);
        });
        test("controlled resistor independently supports reverse terminal current", [] {
            simulation sim;
            electrical IDEAL, IN, OUT, CONTROL, Vss;
            sim.reference(Vss);
            IDEAL = sim_voltage_source(Vss, 1.2_V);
            CONTROL = sim_voltage_source(Vss, .6_V);
            OUT = sim_voltage_source(Vss, 2.4_V);
            IN = resistor(IDEAL, 1_kOhm);
            OUT = sim_controlled_resistor(IN, CONTROL, Vss, 2_kOhm, .35_V, .85_V);
            sim.run(1_us, 100_ns);
            // Half conductance gives 4 kOhm between OUT and IN, feeding back toward IDEAL.
            near(sim.voltage(IN, Vss).value, 1.44);
            near((2.4 - sim.voltage(IN, Vss).value) / 4000, (sim.voltage(IN, Vss).value - 1.2) / 1000, 1e-10);
        });
        test("controlled resistor validates dimensions' runtime values and context", [] {
            simulation first;
            electrical A, IN, Vss;
            expect("E_VALUE", [&] { sim_controlled_resistor(IN, A, Vss, 1_kOhm, .35_V, .35_V); });
            expect("E_INFEASIBLE", [&] { sim_controlled_resistor(IN, A, Vss, 1_kOhm, .85_V, .35_V); });
            expect("E_VALUE", [&] { sim_controlled_resistor(IN, A, Vss, resistance(-1), .35_V, .85_V); });
            simulation second;
            expect("E_CONTEXT", [&] { sim_controlled_resistor(IN, A, Vss, 1_kOhm, .35_V, .85_V); });
        });
        test("C++ model policy types keep electrical interface", [] {
            simulation sim;
            electrical A, Vdd, Vss;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            A = sim_voltage_source(Vss, 0_V, 200_ns, 1.2_V);
            cmos<fast_model> fast;
            cmos<channel_model> detailed;
            bind_inverter(fast, A, Vdd, Vss);
            bind_inverter(detailed, A, Vdd, Vss);
            fast.Y = capacitor(Vss, 10_pF);
            detailed.Y = capacitor(Vss, 10_pF);
            sim.run(400_ns, 1_ns);
            check(sim.voltage(fast.Y, Vss).value < .001 && sim.voltage(detailed.Y, Vss).value < .001,
                  "model endpoint discrepancy");
        });
        test("reversing declaration order preserves physical interaction", [] {
            simulation sim;
            electrical A, Y, Z, Vdd, Vss;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            A = sim_voltage_source(Vss, 0_V, 2_us, 1.2_V);
            auto pmos_vdd = pmos::from(Vdd);
            Y = pmos_vdd(A);
            Vss = nmos(A, Y);
            Y = capacitor(Vss, 10_pF);
            Vss = nmos(A, Z);
            Z = pmos_vdd(A);
            Z = capacitor(Vss, 10_pF);
            sim.observe("Y", Y, Vss);
            sim.observe("Z", Z, Vss);
            sim.run(4_us, 10_ns);
            for (auto& row : sim.trace)
                near(*row.values[0], *row.values[1]);
        });
        test("digital combinational cycle is diagnosed", [] {
            expect("E_DIGITAL_CYCLE", [] {
                simulation sim;
                bits<1> A, B;
                A = sim_invert(B);
                B = sim_invert(A);
                sim.run(1_us, 100_ns);
            });
        });
        test("Newton voltage limiting preserves large source steps", [] {
            simulation sim;
            electrical Vss, IN, OUT;
            sim.reference(Vss);
            IN = sim_voltage_source(Vss, 0_V, 1_us, 100_V);
            OUT = resistor(IN, 1_kOhm);
            Vss = resistor(OUT, 1_kOhm);
            Vss = nmos(Vss, OUT); // Cutoff channel keeps this a transistor-network solver case.
            sim.run(2_us, 1_us);
            near(sim.voltage(OUT, Vss).value, 50);
        });
        std::cout << passed << " C++ semantic checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
