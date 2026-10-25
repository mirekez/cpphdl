#include <VerilogA/export.hpp>
#include <basic/basic.hpp>
#include <basic/level_down.hpp>
#include <basic/nmos_source_follower.hpp>
#include <basic/differential_source_follower.hpp>
#include <basic/buffered_nand_latch.hpp>
#include <basic/cascode_opamp.hpp>
#include <basic/cascode_ota.hpp>
#include <basic/cascode_voltage_buffer.hpp>
#include <basic/shunt_voltage_buffer.hpp>
#include <basic/lead_shunt_voltage_buffer.hpp>

// Reuse the example IC definitions verbatim, without running their testbenches.
#define main inverter_example_main
#include "../examples/cmos_inverter/main.cpp"
#undef main
#define main rc_example_main
#include "../examples/rc_filter/main.cpp"
#undef main
#define main transistor_example_main
#include "../examples/transistor_pair/main.cpp"
#undef main
#define main expression_example_main
#include "../examples/two_transistor_expression/main.cpp"
#undef main

using namespace analog;
using analog::verilog_a::named_port;

template <class Component, class Ports>
void emit(const std::filesystem::path& directory, const std::string& name, Ports ports, bool approximation = false) {
    simulation context;
    Component component;
    component.name(name);
    component.elaborate();
    auto interface = ports(component);
    auto text = approximation ? verilog_a::render_simulation_model(context, name, interface)
                              : verilog_a::render_module(context, name, interface);
    std::filesystem::create_directories(directory);
    verilog_a::detail::write_file(directory / (name + ".va"), text);
}

#define PORT(NAME)                                                                                                     \
    named_port {                                                                                                       \
        #NAME, c.NAME                                                                                                  \
    }
#define MODEL(TYPE, NAME, ...)                                                                                         \
    emit<TYPE>(basic, NAME, [](auto& c) { return std::vector<named_port>{PORT(Vdd), PORT(Vss), __VA_ARGS__}; })

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    try {
        const std::filesystem::path root(argv[1]);
        auto basic = root / "basic/generated";
        MODEL(cmos<channel_model>, "cmos_channel", PORT(A), PORT(Y));
        emit<cmos<fast_model>>(
            basic, "cmos_fast", [](auto& c) { return std::vector<named_port>{PORT(Vdd), PORT(Vss), PORT(A), PORT(Y)}; },
            true);
        emit<level_inverter>(
            basic, "level_inverter",
            [](auto& c) { return std::vector<named_port>{PORT(Vdd), PORT(Vss), PORT(A), PORT(Y)}; }, true);
        MODEL(nmos_differential_pair, "nmos_differential_pair", PORT(PLUS), PORT(MINUS), PORT(LEFT), PORT(RIGHT),
              PORT(TAIL));
        MODEL(pmos_current_mirror, "pmos_current_mirror", PORT(IN), PORT(OUT));
        MODEL(nmos_resistor_amplifier, "nmos_resistor_amplifier", PORT(PLUS), PORT(MINUS), PORT(OUTP), PORT(OUTN));
        MODEL(strongarm_comparator, "strongarm_comparator", PORT(PLUS), PORT(MINUS), PORT(CLOCK), PORT(OUTP), PORT(OUTN));
        MODEL(mos_track_hold, "mos_track_hold", PORT(IN), PORT(CLOCK), PORT(OUT));
        MODEL(level_shifter, "level_shifter", PORT(Vlogic), PORT(A), PORT(Y), PORT(Y_N));
        MODEL(level_down, "level_down", PORT(A), PORT(Y));
        MODEL(nmos_source_follower, "nmos_source_follower", PORT(IN), PORT(OUT));
        MODEL(differential_source_follower, "differential_source_follower", PORT(PLUS), PORT(MINUS), PORT(OUTP), PORT(OUTN));
        MODEL(level_shift_bias, "level_shift_bias", PORT(Vlogic), PORT(BIAS));
        MODEL(cascoded_level_shifter, "cascoded_level_shifter", PORT(Vlogic), PORT(BIAS), PORT(A), PORT(Y), PORT(Y_N));
        MODEL(nand_latch, "nand_latch", PORT(SET_N), PORT(RESET_N), PORT(Q), PORT(Q_N));
        MODEL(cleared_nand_latch, "cleared_nand_latch", PORT(SET_N), PORT(RESET_N), PORT(CLEAR), PORT(Q), PORT(Q_N));
        MODEL(buffered_nand_latch, "buffered_nand_latch", PORT(SET_N), PORT(RESET_N), PORT(CLEAR), PORT(Q), PORT(Q_N));
        MODEL(cmos_d_latch, "cmos_d_latch", PORT(D), PORT(ENABLE), PORT(CLEAR), PORT(Q), PORT(Q_N));
        MODEL(cmos_dff, "cmos_dff", PORT(D), PORT(CLOCK), PORT(CLEAR), PORT(Q), PORT(Q_N));
        MODEL(cmos_divide2, "cmos_divide2", PORT(CLOCK), PORT(CLEAR), PORT(Q), PORT(Q_N));
        MODEL(cmos_phase_frequency_detector, "cmos_phase_frequency_detector", PORT(REF), PORT(FEEDBACK),
              PORT(CLEAR), PORT(UP), PORT(UP_N), PORT(DOWN), PORT(DOWN_N));
        emit<mos_r2r_dac>(basic, "mos_r2r_dac", [](auto& c) {
            std::vector<named_port> ports{PORT(Vdd), PORT(Vss), PORT(OUT)};
            for (unsigned bit = 0; bit < 8; ++bit)
                ports.push_back({"BIT_N" + std::to_string(bit), c.BIT_N[bit]});
            return ports;
        });
        MODEL(opamp, "opamp", PORT(PLUS), PORT(MINUS), PORT(OUT));
        MODEL(cascode_opamp, "cascode_opamp", PORT(PLUS), PORT(MINUS), PORT(OUT),
              PORT(BIAS_N), PORT(BIAS_P), PORT(TAIL_BIAS));
        MODEL(cascode_ota, "cascode_ota", PORT(PLUS), PORT(MINUS), PORT(OUT),
              PORT(BIAS_N), PORT(BIAS_P), PORT(TAIL_BIAS));
        MODEL(cascode_voltage_buffer, "cascode_voltage_buffer", PORT(IN), PORT(OUT),
              PORT(BIAS_N), PORT(BIAS_P), PORT(TAIL_BIAS));
        MODEL(shunt_voltage_buffer, "shunt_voltage_buffer", PORT(IN), PORT(OUT),
              PORT(BIAS_N), PORT(BIAS_P), PORT(TAIL_BIAS));
        MODEL(lead_shunt_voltage_buffer, "lead_shunt_voltage_buffer", PORT(IN), PORT(OUT),
              PORT(BIAS_N), PORT(BIAS_P), PORT(TAIL_BIAS));
        MODEL(non_inverting_amplifier, "non_inverting_amplifier", PORT(IN), PORT(OUT));
        MODEL(regulator, "regulator", PORT(REF), PORT(OUT));
        MODEL(resistor_divider, "resistor_divider", PORT(TOP), PORT(BOTTOM), PORT(TAP));
        MODEL(rc_filter, "rc_filter", PORT(IN), PORT(OUT));
        MODEL(ldo, "ldo", PORT(REF), PORT(OUT));
        MODEL(cmos_nand, "cmos_nand", PORT(A), PORT(B), PORT(Y));
        MODEL(cmos_xor, "cmos_xor", PORT(A), PORT(B), PORT(Y));
        MODEL(current_starved_inverter, "current_starved_inverter", PORT(A), PORT(Y), PORT(CONTROL));
        MODEL(ring_vco, "ring_vco", PORT(CONTROL), PORT(OUT));
        MODEL(lead_lag_filter, "lead_lag_filter", PORT(IN), PORT(OUT));
        MODEL(pll, "pll", PORT(REF), PORT(OUT));
        MODEL(variable_gain_amplifier, "variable_gain_amplifier", PORT(IN), PORT(OUT), PORT(BIAS), PORT(CONTROL));
        MODEL(mos_peak_detector, "mos_peak_detector", PORT(IN), PORT(OUT));
        MODEL(error_integrator, "error_integrator", PORT(IN), PORT(REF), PORT(OUT));
        MODEL(agc, "agc", PORT(IN), PORT(OUT), PORT(BIAS), PORT(REF));

        MODEL(gm_stage, "gm_stage", PORT(PLUS), PORT(MINUS), PORT(BIAS), PORT(OUT));
        MODEL(limiting_amplifier, "limiting_amplifier", PORT(PLUS), PORT(MINUS), PORT(BIAS), PORT(OUT));
        MODEL(bridge_rectifier, "bridge_rectifier", PORT(PLUS), PORT(MINUS), PORT(POS), PORT(NEG));
        emit<sim_sample_hold>(
            basic, "sim_sample_hold",
            [](auto& c) { return std::vector<named_port>{PORT(Vdd), PORT(Vss), PORT(IN), PORT(CLK), PORT(OUT)}; },
            true);
        emit<sim_dff>(
            basic, "sim_dff",
            [](auto& c) { return std::vector<named_port>{PORT(Vdd), PORT(Vss), PORT(D), PORT(CLK), PORT(Q)}; }, true);
        emit<sim_mux2>(
            basic, "sim_mux2",
            [](auto& c) {
                return std::vector<named_port>{PORT(Vdd), PORT(Vss), PORT(A), PORT(B), PORT(SELECT), PORT(Y)};
            },
            true);
        emit<sim_xor>(
            basic, "sim_xor",
            [](auto& c) { return std::vector<named_port>{PORT(Vdd), PORT(Vss), PORT(A), PORT(B), PORT(Y)}; }, true);

        auto example = [&](const std::string& name) { return root / "examples/generated" / name; };
        auto inverter_ports = [](auto& c) { return std::vector<named_port>{PORT(Vdd), PORT(Vss), PORT(A), PORT(Y)}; };
        emit<Inverter>(example("cmos_inverter"), "Inverter", inverter_ports);
        emit<NamedDevices>(example("transistor_pair"), "NamedDevices", inverter_ports);
        emit<WireOutput>(example("two_transistor_expression"), "WireOutput", inverter_ports);
        emit<ActiveOutput>(example("two_transistor_expression"), "ActiveOutput", inverter_ports);
        emit<LowPass>(example("rc_filter"), "LowPass",
                      [](auto& c) { return std::vector<named_port>{PORT(Vdd), PORT(Vss), PORT(IN), PORT(OUT)}; });
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#undef MODEL
#undef PORT
