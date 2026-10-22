#include "../phy.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;
using namespace analog::ten_g;

namespace {
using points = std::vector<std::pair<double, double>>;
std::vector<int> pattern(unsigned count) {
    std::vector<int> result;
    unsigned state = 0x5d;
    for (unsigned i = 0; i < count; ++i) {
        if (i < 24)
            result.push_back(i % 2);
        else {
            result.push_back(state & 1);
            state = ((state << 1) | (((state >> 6) ^ (state >> 5)) & 1)) & 127;
        }
    }
    return result;
}
points sampled(unsigned count, double period, auto value, double initial = 0, double offset = 0) {
    points wave{{0, initial}};
    double previous = initial;
    for (unsigned i = 0; i < count; ++i) {
        double time = (i + .04) * period + offset;
        double next = value(i);
        if (next != previous) {
            if (time > wave.back().first)
                wave.emplace_back(time, previous);
            wave.emplace_back(time + .04 * period, next);
        }
        previous = next;
    }
    return wave;
}
points clock_wave(double stop, double period, double phase = 0) {
    points wave{{0, 0}};
    for (unsigned i = 0; (i + .5) * period + phase < stop; ++i) {
        double rise = (i + .5) * period + phase;
        double fall = (i + 1) * period + phase;
        if (rise > wave.back().first) {
            wave.emplace_back(rise, 0);
            wave.emplace_back(rise + .025 * period, supply);
        }
        if (fall > wave.back().first && fall < stop) {
            wave.emplace_back(fall, supply);
            wave.emplace_back(fall + .025 * period, 0);
        }
    }
    return wave;
}
void bind(IC& component, const electrical& vdd, const electrical& vss) {
    component.Vdd = vdd;
    component.Vss = vss;
    component.elaborate();
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 5)
        return 2;
    try {
        const std::string name(argv[1]), mode(argv[3]);
        const std::filesystem::path out(argv[2]);
        double refinement = std::stod(argv[4]);
        if (refinement != 1 && refinement != 2)
            throw std::runtime_error("refinement must be 1 or 2");
        simulation sim;
        sim.minimum_step = duration(ui / 65536);
        sim.analytic_jacobian = true;
        electrical vdd, vss, vcm, input, p, n, clk;
        sim.reference(vss);
        vdd = sim_voltage_source(vss, voltage(supply));
        vcm = sim_voltage_source(vss, voltage(common_mode));
        double bit_time = name == "sfi_decay" ? 1e-6 : ui;
        double phase = name == "cdr_phase" ? .23 : 0;
        if (name == "cdr_offset")
            bit_time = ui / 1.0001;
        if (name == "cdr_offset_negative")
            bit_time = ui / .9999;
        bool framed = name == "link" || name == "link_loaded" || name == "receiver";
        unsigned count = framed                                             ? 192
                         : name == "cdr_gap"                                ? 256
                         : name == "cdr_ppm"                                ? 320
                         : name.starts_with("cdr") && name != "cdr_no_data" ? 160
                                                                            : 96;
        auto bits = pattern(count + 8);
        if (framed) {
            bits[112] = 0;
            bits[113] = 1; // Data sync header in this bench's transmitted-bit order.
            std::uint64_t state = (std::uint64_t(1) << 58) - 1;
            constexpr std::uint64_t payload = 0x0123456789abcdefULL;
            for (unsigned i = 0; i < 64; ++i) {
                int encoded = ((payload >> i) ^ (state >> 38) ^ (state >> 57)) & 1;
                bits[114 + i] = encoded;
                state = ((state << 1) | encoded) & ((std::uint64_t(1) << 58) - 1);
            }
        }
        if (name == "cdr_gap") {
            for (unsigned i = 0; i < 80; ++i)
                bits[i] = i % 2;
            for (unsigned i = 80; i < 146; ++i)
                bits[i] = 0;
        }
        double stop = count * bit_time;
        double step = bit_time / ((name == "vco" || name == "cdr_no_data" ? 512 : 256) * refinement);
        auto observe = [&](const char* label, const electrical& node) { sim.observe(label, node, vss); };
        auto nrz = [&](const electrical& node, double low, double high, int shift = 0) {
            node.owner().connect(node, sim_pwl_voltage_source(vss, sampled(
                                                                       count + 2, bit_time,
                                                                       [&](unsigned i) {
                                                                           int index = int(i) + shift;
                                                                           return index < 0 || index >= int(bits.size())
                                                                                      ? low
                                                                                  : bits[index] ? high
                                                                                                : low;
                                                                       },
                                                                       common_mode, phase * bit_time)));
        };
        auto differential = [&](double amplitude, double isi = 0) {
            auto value = [&](unsigned i) {
                double current = bits[i] ? 1 : -1;
                double previous = i && bits[i - 1] ? 1 : -1;
                return amplitude * current + isi * previous + (name == "cdr_noisy" ? .015 * std::sin(i * 2.399963) : 0);
            };
            p = sim_pwl_voltage_source(vss,
                                       sampled(
                                           count + 2, bit_time, [&](unsigned i) { return common_mode + value(i) / 2; },
                                           common_mode, phase * bit_time));
            n = sim_pwl_voltage_source(vss,
                                       sampled(
                                           count + 2, bit_time, [&](unsigned i) { return common_mode - value(i) / 2; },
                                           common_mode, phase * bit_time));
        };
        auto clocks = [&] { clk = sim_pwl_voltage_source(vss, clock_wave(stop + 2 * ui, bit_time)); };
        auto parallel_inputs = [&](auto& component) {
            for (unsigned lane = 0; lane < 4; ++lane)
                component.DATA[lane] =
                    sim_pwl_voltage_source(vss, sampled(count / 4 + 2, 4 * bit_time, [&](unsigned word) {
                                               return bits[(4 * word + lane) % bits.size()] ? supply : 0;
                                           }));
            component.CLOCK_HALF = sim_pwl_voltage_source(vss, clock_wave(stop + 4 * ui, 2 * bit_time, .03 * bit_time));
            component.CLOCK_QUARTER =
                sim_pwl_voltage_source(vss, clock_wave(stop + 4 * ui, 4 * bit_time, .03 * bit_time));
        };
        if (name == "primitives") {
            electrical source, junction, sense, amp, clipped;
            source = sim_pwl_voltage_source(vss, {{0, 0}, {ui, 0}, {ui + .05 * ui, 1}});
            junction = resistor(source, resistance(90));
            junction = inductor(sense, inductance_value(5e-9));
            vss = resistor(sense, resistance(10));
            amp = resistor(vss, resistance(100));
            amp = transconductor(vss, source, vss, transconductance(.002));
            clipped = resistor(source, resistance(100));
            clipped = diode(vss, resistance(10), voltage(.2));
            observe("RL_SENSE", sense);
            observe("GM", amp);
            observe("DIODE", clipped);
            observe("INPUT", source);
            stop = 12 * ui;
        } else if (name == "gm_stage" || name == "limiter") {
            input = sim_pwl_voltage_source(
                vss, {{0, common_mode}, {ui, common_mode}, {1.05 * ui, .5}, {5 * ui, .5}, {5.05 * ui, .7}});
            if (name == "gm_stage") {
                gm_stage stage;
                stage.PLUS = input;
                stage.MINUS = vcm;
                stage.BIAS = vcm;
                bind(stage, vdd, vss);
                observe("OUT", stage.OUT);
            } else {
                limiting_amplifier stage;
                stage.gain = transconductance(.1);
                stage.PLUS = input;
                stage.MINUS = vcm;
                stage.BIAS = vcm;
                bind(stage, vdd, vss);
                observe("OUT", stage.OUT);
            }
            stop = 10 * ui;
        } else if (name == "rectifier") {
            differential(.4);
            bridge_rectifier bridge;
            bridge.PLUS = p;
            bridge.MINUS = n;
            bind(bridge, vdd, vss);
            sim.observe("RECTIFIED", bridge.POS, bridge.NEG);
        } else if (name == "sample_hold" || name == "dff" || name == "mux2" || name == "xor" || name == "history" ||
                   name == "deserializer") {
            nrz(input, 0, supply);
            clocks();
            if (name == "sample_hold") {
                sim_sample_hold stage;
                stage.IN = input;
                stage.CLK = clk;
                bind(stage, vdd, vss);
                observe("OUT", stage.OUT);
            } else if (name == "dff") {
                sim_dff stage;
                stage.D = input;
                stage.CLK = clk;
                bind(stage, vdd, vss);
                observe("OUT", stage.Q);
            } else if (name == "xor") {
                sim_xor stage;
                stage.A = input;
                stage.B = clk;
                bind(stage, vdd, vss);
                observe("OUT", stage.Y);
            } else if (name == "mux2") {
                electrical opposite;
                nrz(opposite, supply, 0);
                sim_mux2 stage;
                stage.A = input;
                stage.B = opposite;
                stage.SELECT = clk;
                bind(stage, vdd, vss);
                observe("OUT", stage.Y);
            } else if (name == "history") {
                tap_history stage;
                stage.IN = input;
                stage.CLOCK = clk;
                bind(stage, vdd, vss);
                for (unsigned i = 0; i < 3; ++i)
                    observe(("D" + std::to_string(i)).c_str(), stage.history[i].Q);
            } else {
                deserializer stage;
                stage.IN = input;
                stage.CLOCK = clk;
                bind(stage, vdd, vss);
                for (unsigned i = 0; i < 4; ++i)
                    observe(("D" + std::to_string(i)).c_str(), stage.DATA[i]);
            }
        } else if (name == "serializer") {
            serializer stage;
            parallel_inputs(stage);
            bind(stage, vdd, vss);
            observe("OUT", stage.OUT);
        } else if (name == "emphasis") {
            tx_emphasis stage;
            nrz(stage.PRE, 0, supply, 1);
            nrz(stage.MAIN, 0, supply);
            nrz(stage.POST, 0, supply, -1);
            stage.VCM = vcm;
            bind(stage, vdd, vss);
            stage.N = resistor(stage.P, resistance(100));
            sim.observe("DIFF", stage.P, stage.N);
        } else if (name == "channel") {
            differential(.4);
            channel stage;
            stage.P = p;
            stage.N = n;
            stage.VCM = vcm;
            bind(stage, vdd, vss);
            sim.observe("DIFF", stage.OUTP, stage.OUTN);
        } else if (name == "sfi" || name == "sfi_decay") {
            if (name == "sfi") {
                differential(.4);
            } else {
                p = sim_pwl_voltage_source(vss, {{0, .6}, {bit_time, .6}, {1.04 * bit_time, .8}});
                n = sim_pwl_voltage_source(vss, {{0, .6}, {bit_time, .6}, {1.04 * bit_time, .4}});
            }
            sfi_input stage;
            stage.P = p;
            stage.N = n;
            stage.VCM = vcm;
            bind(stage, vdd, vss);
            sim.observe("DIFF", stage.OUTP, stage.OUTN);
            observe("P", stage.OUTP);
            observe("N", stage.OUTN);
        } else if (name == "ctle_low" || name == "ctle_nyquist") {
            double frequency = name == "ctle_low" ? 1e9 : line_rate / 2;
            auto sine = [&](double sign) {
                points wave;
                for (unsigned i = 0; i <= 96 * 16; ++i) {
                    double time = i * bit_time / 16;
                    wave.emplace_back(time,
                                      common_mode + sign * .01 * std::sin(2 * std::numbers::pi * frequency * time));
                }
                return wave;
            };
            p = sim_pwl_voltage_source(vss, sine(1));
            n = sim_pwl_voltage_source(vss, sine(-1));
            ctle stage;
            stage.P = p;
            stage.N = n;
            stage.VCM = vcm;
            bind(stage, vdd, vss);
            sim.observe("OUT", stage.OUTP, stage.OUTN);
        } else if (name == "lock_monitor" || name == "lock_monitor_bad") {
            lock_monitor stage;
            electrical error, reference;
            reference = sim_pwl_voltage_source(vss, clock_wave(stop + 2 * ui, bit_time));
            if (name == "lock_monitor")
                error = reference;
            else
                error = sim_voltage_source(vss, voltage(1.2));
            stage.ERROR = error;
            stage.REFERENCE = reference;
            stage.VCM = vcm;
            bind(stage, vdd, vss);
            observe("LOCK", stage.LOCK);
        } else if (name == "vco") {
            electrical control;
            control = sim_voltage_source(vss, voltage(.7));
            vco_10g stage;
            stage.CONTROL = control;
            bind(stage, vdd, vss);
            observe("CLOCK", stage.OUT);
        } else if (name == "hogge") {
            nrz(input, 0, supply);
            clocks();
            hogge_detector stage;
            stage.DATA = input;
            stage.CLOCK = clk;
            bind(stage, vdd, vss);
            observe("ERROR", stage.ERROR);
            observe("REFERENCE", stage.REFERENCE);
        } else if (name.starts_with("cdr")) {
            if (name == "cdr_no_data")
                std::fill(bits.begin(), bits.end(), 0);
            differential(.4);
            pll_cdr stage;
            stage.P = p;
            stage.N = n;
            stage.VCM = vcm;
            bind(stage, vdd, vss);
            observe("CLOCK", stage.CLOCK);
            observe("LOCK", stage.LOCK);
            observe("IMBALANCE", stage.qualification.IMBALANCE);
            observe("ENVELOPE", stage.qualification.ENVELOPE);
            observe("ACTIVITY", stage.qualification.ACTIVITY);
            observe("CONTROL", stage.CONTROL);
            observe("ERROR", stage.detector.ERROR);
            observe("REFERENCE", stage.detector.REFERENCE);
        } else if (name == "dfe" || name == "dfe_bypass") {
            differential(.08, .09);
            clocks();
            dfe stage;
            stage.P = p;
            stage.N = n;
            stage.VCM = vcm;
            stage.CLOCK = clk;
            stage.tap = name == "dfe" ? .09 : 0;
            bind(stage, vdd, vss);
            observe("Q", stage.Q);
            observe("SUM", stage.SUM);
            observe("CLOCK", clk);
        } else if (name == "transmitter") {
            clocks();
            transmitter stage;
            parallel_inputs(stage);
            stage.CLOCK = clk;
            stage.VCM = vcm;
            bind(stage, vdd, vss);
            stage.N = resistor(stage.P, resistance(100));
            sim.observe("DIFF", stage.P, stage.N);
        } else if (name == "receiver") {
            differential(.4, .025);
            receiver stage;
            stage.P = p;
            stage.N = n;
            stage.VCM = vcm;
            bind(stage, vdd, vss);
            observe("CLOCK", stage.recovery.CLOCK);
            observe("LOCK", stage.recovery.LOCK);
            observe("Q", stage.feedback.Q);
            for (unsigned i = 0; i < 4; ++i)
                observe(("D" + std::to_string(i)).c_str(), stage.DATA[i]);
        } else if (name == "link" || name == "link_loaded") {
            clocks();
            link stage;
            parallel_inputs(stage);
            stage.CLOCK = clk;
            stage.VCM = vcm;
            if (name == "link_loaded")
                stage.interface.termination = resistance(80);
            bind(stage, vdd, vss);
            observe("CLOCK", stage.rx.recovery.CLOCK);
            observe("LOCK", stage.rx.recovery.LOCK);
            observe("Q", stage.rx.feedback.Q);
            for (unsigned i = 0; i < 4; ++i)
                observe(("D" + std::to_string(i)).c_str(), stage.rx.DATA[i]);
            sim.observe("TX", stage.tx.P, stage.tx.N);
            sim.observe("RX", stage.medium.OUTP, stage.medium.OUTN);
        } else {
            throw std::runtime_error("unknown test: " + name);
        }
        if (name == "cdr_noisy")
            for (auto& element : sim.elements)
                if (element.kind == law::sim_voltage_source && element.waveform.size() > 1)
                    for (auto& point : element.waveform)
                        if (point.first > 0) {
                            double symbol = std::floor(point.first / bit_time);
                            point.first += .02 * bit_time * std::sin(symbol * .73);
                        }
        std::filesystem::create_directories(out);
        verilog_a::write_experiment(sim, out,
                                    {duration(stop), duration(step), duration(bit_time / (512 * refinement)), 1e-16, 2,
                                     name.starts_with("link") ? 1e-9 : 0});
        std::ofstream contract(out / "contract.json");
        contract << std::setprecision(17) << "{\"case\":" << json_string(name) << ",\"ui_s\":" << bit_time
                 << ",\"acquisition_ui\":" << ((framed || (name.starts_with("cdr") && name != "cdr_no_data")) ? 80 : 30)
                 << ",\"stop_s\":" << stop << ",\"phase_ui\":" << phase << ",\"framed\":" << (framed ? "true" : "false")
                 << ",\"bits\":[";
        for (std::size_t i = 0; i < bits.size(); ++i)
            contract << (i ? "," : "") << bits[i];
        contract << "]}\n";
        if (mode == "run") {
            sim.run(duration(stop), duration(step));
            sim.write(out / "cpp");
            if (sim.status() != "sampled_pass")
                return 1;
        } else if (mode != "export") {
            throw std::runtime_error("mode must be run or export");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
