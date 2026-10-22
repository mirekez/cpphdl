#include "../phy.hpp"
#include "../mapping.hpp"
#include "../IHPSG13G2/current_steering_driver.hpp"
#include "../IHPSG13G2/biased_driver.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>
#include <numbers>

using namespace analog;
using namespace analog::one_g;

namespace {
using points = std::vector<std::pair<double, double>>;
using symbols = std::vector<quartet>;
symbols pattern(unsigned count, unsigned seed) {
    symbols result(count);
    for (unsigned i = 0; i < count; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        result[i] = symbol_mapper::map((seed >> 3) & 63, seed & 7);
    }
    return result;
}
points sampled(unsigned count, auto value, double initial, double offset = 0) {
    points wave{{0, initial}};
    double previous = initial;
    for (unsigned i = 0; i < count; ++i) {
        double time = (i + .15 + offset) * ui;
        double next = value(i);
        if (next != previous) {
            wave.emplace_back(time, previous);
            wave.emplace_back(time + .04 * ui, next);
        }
        previous = next;
    }
    return wave;
}
points clock_wave(unsigned count, double phase = .02) {
    points wave{{0, 0}};
    for (unsigned i = 0; i < count; ++i) {
        wave.emplace_back((i + phase) * ui, 0);
        wave.emplace_back((i + phase + .025) * ui, logic_supply);
        wave.emplace_back((i + phase + .5) * ui, logic_supply);
        wave.emplace_back((i + phase + .525) * ui, 0);
    }
    return wave;
}
void write_symbols(std::ostream& out, const symbols& sequence) {
    out << '[';
    for (unsigned i = 0; i < sequence.size(); ++i) {
        out << (i ? ",[" : "[");
        for (unsigned lane = 0; lane < pairs; ++lane)
            out << (lane ? "," : "") << sequence[i][lane];
        out << ']';
    }
    out << ']';
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 5)
        return 2;
    try {
        const std::string name(argv[1]), mode(argv[3]);
        const std::filesystem::path out(argv[2]);
        unsigned refinement = unsigned(std::stoi(argv[4]));
        if (refinement != 1 && refinement != 2)
            throw std::runtime_error("refinement must be 1 or 2");
        bool system = name.starts_with("link") || name == "pair_link";
        unsigned count = name == "link_loaded" ? 512 : system ? 256 : 64;
        unsigned training = name == "link_loaded" ? 416 : 160;
        auto a = pattern(count + 3, 0x13579b), b = pattern(count + 3, 0xa91e37);
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(ui / 65536);
        electrical vdd, vss, vcm, vref, logic, clock, input;
        sim.reference(vss);
        vdd = sim_voltage_source(vss, voltage(supply));
        vcm = sim_voltage_source(vss, voltage(common_mode));
        vref = sim_voltage_source(vss, voltage(logic_midpoint));
        logic = sim_voltage_source(vss, voltage(logic_supply));
        clock = sim_pwl_voltage_source(vss, clock_wave(count + 2, name.starts_with("dfe") ? .55 : .02));
        auto signal = [&](const electrical& node, auto value, double initial = common_mode) {
            node.owner().connect(node, sim_pwl_voltage_source(vss, sampled(count + 2, value, initial)));
        };
        auto observe = [&](std::string label, const electrical& node) { sim.observe(label, node, vcm); };
        auto codes = [&](auto& ports, const symbols& sequence, unsigned lane) {
            for (unsigned bit = 0; bit < 4; ++bit)
                signal(
                    ports[bit],
                    [&, bit, lane](unsigned i) { return sequence[i][lane] + 2 > int(bit) ? logic_supply : 0; },
                    bit < 2 ? logic_supply : 0);
        };
        auto tx_ports = [&](transmitter& tx, const symbols& sequence, unsigned lane) {
            codes(tx.CODE, sequence, lane);
            tx.CLOCK = clock;
            tx.VREF = vref;
            tx.VCM = vcm;
        };
        if (name == "dac" || name == "dac_loaded") {
            pam5_dac dac;
            codes(dac.CODE, a, 0);
            dac.VREF = vref;
            dac.VCM = vcm;
            bind(dac, vdd, vss);
            if (name == "dac_loaded")
                vcm = resistor(dac.OUT, resistance(1000));
            observe("OUT", dac.OUT);
        } else if (name == "mos_driver" || name == "biased_driver" || name == "trimmed_driver") {
            auto connect_driver = [&](auto& driver) {
                for (unsigned bit = 0; bit < 4; ++bit) {
                    signal(
                        driver.GATE_N[bit], [&, bit](unsigned i) { return a[i][0] + 2 > int(bit) ? supply : 0; },
                        bit < 2 ? supply : 0);
                    signal(
                        driver.GATE_P[bit], [&, bit](unsigned i) { return a[i][0] + 2 > int(bit) ? 0 : supply; },
                        bit < 2 ? 0 : supply);
                }
                bind(driver, vdd, vss);
                driver.N = resistor(driver.P, resistance(100));
                sim.observe("DIFF", driver.P, driver.N);
                sim.observe("P_ABS", driver.P, vss);
                sim.observe("N_ABS", driver.N, vss);
            };
            if (name == "mos_driver") {
                ihp_sg13g2::current_steering_driver driver;
                connect_driver(driver);
            } else if (name == "biased_driver") {
                ihp_sg13g2::biased_pam5_driver driver;
                connect_driver(driver);
                sim.observe("BIAS", driver.bias, vss);
            } else {
                ihp_sg13g2::trimmed_pam5_driver driver;
                for (unsigned bit = 0; bit < 6; ++bit)
                    driver.reference.TRIM_N[bit] = sim_voltage_source(vss, voltage((31 & (1 << bit)) ? 0 : supply));
                connect_driver(driver);
                sim.observe("BIAS", driver.bias, vss);
            }
        } else if (name == "driver" || name == "driver_loaded") {
            signal(input, [&](unsigned i) { return common_mode + .5 * a[i][0]; });
            line_driver driver;
            driver.IN = input;
            driver.VCM = vcm;
            bind(driver, vdd, vss);
            driver.N = resistor(driver.P, resistance(name == "driver" ? 100 : 50));
            sim.observe("DIFF", driver.P, driver.N);
            observe("P", driver.P);
            observe("N", driver.N);
        } else if (name == "memory") {
            signal(input, [&](unsigned i) { return common_mode + symbol_step * a[i][0]; });
            sim_symbol_memory memory;
            memory.IN = input;
            memory.CLOCK = clock;
            bind(memory, vdd, vss);
            observe("OUT", memory.OUT);
        } else if (name == "pulse_shaper") {
            electrical previous;
            signal(input, [&](unsigned i) { return common_mode + .5 * a[i][0]; });
            signal(previous, [&](unsigned i) { return common_mode + .5 * (i ? a[i - 1][0] : 0); });
            pulse_shaper filter;
            filter.MAIN = input;
            filter.HISTORY = previous;
            filter.VCM = vcm;
            bind(filter, vdd, vss);
            observe("OUT", filter.OUT);
        } else if (name == "slicer") {
            signal(input, [&](unsigned i) { return common_mode + symbol_step * a[i][0]; });
            sim_pam5_slicer slicer;
            slicer.IN = input;
            slicer.VCM = vcm;
            slicer.VLOGIC = logic;
            bind(slicer, vdd, vss);
            for (unsigned bit = 0; bit < 4; ++bit)
                sim.observe("C" + std::to_string(bit), slicer.CODE[bit], vss);
        } else if (name == "hybrid" || name == "hybrid_bad" || name == "receiver") {
            electrical p, n, local;
            signal(p, [&](unsigned i) { return common_mode + (.25 / 1.15 * b[i][0] + .3 * a[i][0]) / 2; });
            signal(n, [&](unsigned i) { return common_mode - (.25 / 1.15 * b[i][0] + .3 * a[i][0]) / 2; });
            signal(local, [&](unsigned i) { return common_mode + .5 * a[i][0]; });
            if (name == "receiver") {
                receiver rx;
                rx.P = p;
                rx.N = n;
                rx.LOCAL = local;
                rx.VCM = vcm;
                rx.cancellation.balance = .6;
                bind(rx, vdd, vss);
                observe("OUT", rx.OUT);
            } else {
                hybrid block;
                block.P = p;
                block.N = n;
                block.LOCAL = local;
                block.VCM = vcm;
                block.balance = name == "hybrid" ? .6 : .2;
                bind(block, vdd, vss);
                observe("OUT", block.OUT);
            }
        } else if (name == "channel" || name == "channel_loaded" || name == "channel_reverse") {
            electrical zero;
            zero = vcm;
            signal(input, [&](unsigned i) { return common_mode + .5 * a[i][0]; });
            line_driver left, right;
            left.IN = name == "channel_reverse" ? zero : input;
            right.IN = name == "channel_reverse" ? input : zero;
            left.VCM = vcm;
            right.VCM = vcm;
            right.source = resistance(name == "channel_loaded" ? 35 : 50);
            bind(left, vdd, vss);
            bind(right, vdd, vss);
            cable_pair medium;
            medium.AP = left.P;
            medium.AN = left.N;
            medium.BP = right.P;
            medium.BN = right.N;
            medium.VCM = vcm;
            bind(medium, vdd, vss);
            sim.observe("NEAR", left.P, left.N);
            sim.observe("FAR", right.P, right.N);
        } else if (name == "ctle_low" || name == "ctle_nyquist") {
            double frequency = name == "ctle_low" ? symbol_rate / 16 : symbol_rate / 2;
            points wave;
            for (unsigned i = 0; i <= count * 64; ++i) {
                double time = i * ui / 64;
                wave.emplace_back(time, common_mode + .02 * std::sin(2 * std::numbers::pi * frequency * time));
            }
            input = sim_pwl_voltage_source(vss, wave);
            ctle equalizer;
            equalizer.IN = input;
            equalizer.VCM = vcm;
            bind(equalizer, vdd, vss);
            observe("OUT", equalizer.OUT);
        } else if (name == "dfe" || name == "dfe_bypass") {
            signal(input,
                   [&](unsigned i) { return common_mode + symbol_step * (a[i][0] + .35 * (i ? a[i - 1][0] : 0)); });
            dfe feedback;
            feedback.IN = input;
            feedback.CLOCK = clock;
            feedback.VCM = vcm;
            feedback.VREF = vref;
            feedback.VLOGIC = logic;
            feedback.tap = name == "dfe" ? .35 : 0;
            bind(feedback, vdd, vss);
            observe("OUT", feedback.OUT);
            observe("SUM", feedback.SUM);
        } else if (name == "transmitter") {
            transmitter tx;
            tx_ports(tx, a, 0);
            bind(tx, vdd, vss);
            tx.N = resistor(tx.P, resistance(100));
            sim.observe("DIFF", tx.P, tx.N);
            observe("TARGET", tx.REPLICA);
        } else if (name == "pair_link") {
            pair_link block;
            codes(block.A, a, 0);
            codes(block.B, b, 0);
            block.CLOCK = clock;
            block.VREF = vref;
            block.VCM = vcm;
            block.rx_a.cancellation.balance = .35;
            block.rx_b.cancellation.balance = .35;
            bind(block, vdd, vss);
            observe("TXA0", block.tx_a.REPLICA);
            observe("TXB0", block.tx_b.REPLICA);
            observe("RXA0", block.OUTA);
            observe("RXB0", block.OUTB);
            sim.observe("LINEA0", block.tx_a.P, block.tx_a.N);
            sim.observe("LINEB0", block.tx_b.P, block.tx_b.N);
        } else if (name.starts_with("link")) {
            link block;
            for (unsigned lane = 0; lane < pairs; ++lane) {
                codes(block.A[lane], a, lane);
                codes(block.B[lane], b, lane);
                block.rx_a[lane].cancellation.balance = .35;
                block.rx_b[lane].cancellation.balance = .35;
                if (name == "link_loaded")
                    block.tx_b[lane].driver.source = resistance(35);
                if (name == "link_noisy") {
                    electrical noise;
                    points wave;
                    for (unsigned i = 0; i <= count * 32; ++i)
                        wave.emplace_back(i * ui / 32, std::sin(i * 2.399963 + lane * 1.734));
                    noise = sim_pwl_voltage_source(vss, wave);
                    block.OUTA[lane] = transconductor(vss, noise, vss, transconductance(3e-6));
                    block.OUTB[lane] = transconductor(vss, vss, noise, transconductance(3e-6));
                }
            }
            block.CLOCK = clock;
            block.VREF = vref;
            block.VCM = vcm;
            bind(block, vdd, vss);
            for (unsigned lane = 0; lane < pairs; ++lane) {
                std::string suffix = std::to_string(lane);
                observe("TXA" + suffix, block.tx_a[lane].REPLICA);
                observe("TXB" + suffix, block.tx_b[lane].REPLICA);
                observe("RXA" + suffix, block.OUTA[lane]);
                observe("RXB" + suffix, block.OUTB[lane]);
                sim.observe("LINEA" + suffix, block.tx_a[lane].P, block.tx_a[lane].N);
                sim.observe("LINEB" + suffix, block.tx_b[lane].P, block.tx_b[lane].N);
            }
        } else {
            throw std::runtime_error("unknown 1G bench: " + name);
        }
        double stop = count * ui, step = ui / (128 * refinement);
        std::filesystem::create_directories(out);
        // Stamp charges in the SPICE host to avoid flattened OSDI ddt conditioning failures.
        // Each capacitor retains the same terminals, value and initial voltage.
        verilog_a::write_experiment(sim, out, {duration(stop), duration(step), duration(step / 2), 1e-16, 2, 1e-18});
        std::ofstream contract(out / "contract.json");
        contract << std::setprecision(17) << "{\"case\":" << json_string(name) << ",\"ui_s\":" << ui
                 << ",\"stop_s\":" << stop << ",\"count\":" << count << ",\"training_symbols\":" << training
                 << ",\"sample_phase\":0.8,\"A\":";
        write_symbols(contract, a);
        contract << ",\"B\":";
        write_symbols(contract, b);
        contract << "}\n";
        contract.close();
        if (mode == "run") {
            sim.run(duration(stop), duration(step));
            sim.write(out / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        if (mode != "export")
            throw std::runtime_error("mode must be run or export");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
