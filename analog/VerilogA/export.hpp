#pragma once
#include <analog.hpp>
#include <set>
#include <string_view>

namespace analog::verilog_a {

struct named_port {
    std::string name;
    electrical terminal;
};

struct transient {
    duration stop;
    duration step;
    // ngspice integrates independently with smaller adaptive steps, then comparisons interpolate its output.
    duration maximum_step;
    double charge_tolerance = 1e-14; // Coulombs; sub-fF storage needs a tighter value than SPICE's default.
    unsigned spice_order = 2;
    // Optional experiment-only placement of large linear capacitors in the SPICE deck.
    // Reusable Verilog-A modules always retain their complete circuit laws.
    double spice_capacitance_threshold = 0;
    // Zero selects the automatic near-ideal experiment edge. An explicit
    // edge keeps the stimulus fixed when integration steps are refined.
    duration driver_edge{0};
};

namespace detail {
inline bool identifier(std::string_view name) {
    if (name.empty() || !((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_'))
        return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    static const std::set<std::string_view> reserved{
        "analog", "begin",  "end",      "module",      "endmodule",  "electrical", "inout",
        "input",  "output", "real",     "integer",     "parameter",  "branch",     "ground",
        "if",     "else",   "function", "endfunction", "initial",    "always",     "wire",
        "reg",    "assign", "generate", "endgenerate", "discipline", "nature"};
    return !reserved.contains(name) && !name.starts_with("cpphdl_");
}
inline void fail(const std::string& message) {
    throw diagnostic("E_VERILOGA", message);
}
inline std::string number(double value) {
    if (!std::isfinite(value))
        fail("nonfinite model parameter");
    std::ostringstream text;
    text << std::setprecision(17) << value;
    auto result = text.str();
    if (result.find_first_of(".eE") == std::string::npos)
        result += ".0";
    return result;
}
inline std::string comment(std::string value) {
    for (char& c : value)
        if (c == '\r' || c == '\n')
            c = ' ';
    return value;
}
inline std::vector<std::size_t> terminals(const element& e) {
    switch (e.kind) {
    case law::resistor:
    case law::inductor:
    case law::diode:
    case law::capacitor:
    case law::sim_voltage_source:
        return {e.a, e.b};
    case law::nmos:
    case law::pmos:
        return {e.a, e.b, e.gate};
    case law::transconductor:
    case law::sim_controlled_resistor:
        return {e.a, e.b, e.gate, e.reference};
    case law::sim_driver:
        return {e.a, e.vdd, e.vss};
    }
    fail("unknown device law");
    return {};
}
inline void validate(const simulation& sim, bool experiment, bool partition = false) {
    for (const auto& c : sim.components())
        if (c.state != component_state::ready)
            fail("IC " + c.name + " must be successfully elaborated before export");
    if (!partition && !sim.operations.empty())
        fail("managed/native digital operations need a mixed-signal backend; Verilog-A export cannot erase them");
    for (const auto& e : sim.elements) {
        if (!experiment &&
            (e.kind == law::sim_voltage_source || e.kind == law::sim_driver || e.kind == law::sim_controlled_resistor))
            fail("simulation-only device in reusable IC; use write_experiment for an explicit simulation bench");
        for (auto node : terminals(e))
            (void)sim.root(node);
    }
}
inline std::set<std::size_t> nodes(const simulation& sim) {
    std::set<std::size_t> result;
    for (const auto& e : sim.elements)
        for (auto node : terminals(e))
            result.insert(sim.root(node));
    return result;
}
inline const char* mos_function = R"VA(
    // Square-law channel, including reverse conduction. This is not a BSIM model.
    analog function real cpphdl_channel;
        input vgs, vds, beta, threshold, polarity;
        real vgs, vds, beta, threshold, polarity;
        real gate, drain, direction, overdrive, current;
        begin
            gate = polarity * vgs;
            drain = polarity * vds;
            direction = 1.0;
            if (drain < 0.0) begin
                gate = gate - drain;
                drain = -drain;
                direction = -1.0;
            end
            overdrive = gate - threshold;
            current = 0.0;
            if (overdrive > 0.0) begin
                if (drain < overdrive)
                    current = beta * (overdrive * drain - drain * drain / 2.0);
                else
                    current = beta * overdrive * overdrive / 2.0;
            end
            cpphdl_channel = polarity * direction * current;
        end
    endfunction
)VA";

struct node_port {
    std::string name;
    std::size_t id;
};
inline std::string module(const simulation& sim, const std::string& name, const std::vector<node_port>& ports,
                          bool experiment, bool partition = false, double spice_capacitance_threshold = 0) {
    validate(sim, experiment, partition);
    if (!identifier(name) || ports.empty())
        fail("module name must be a nonreserved identifier and its port list must be explicit");
    std::map<std::size_t, std::string> names;
    std::set<std::string> used_names;
    std::vector<std::pair<std::string, std::string>> aliases;
    for (const auto& p : ports) {
        if (!identifier(p.name) || !used_names.insert(p.name).second)
            fail("invalid or duplicate port name: " + p.name);
        auto id = sim.root(p.id);
        if (auto found = names.find(id); found != names.end())
            aliases.emplace_back(p.name, found->second);
        else
            names[id] = p.name;
    }
    auto active_nodes = nodes(sim);
    if (active_nodes.contains(0) && !names.contains(0))
        fail("expose the simulation reference as an explicit electrical port");
    for (auto node : active_nodes)
        if (!names.contains(node))
            names[node] = "cpphdl_n" + std::to_string(node);
    auto node = [&](std::size_t id) { return names.at(sim.root(id)); };
    auto branch = [&](std::size_t a, std::size_t b) { return "(" + node(a) + ", " + node(b) + ")"; };
    std::ostringstream out;
    out << "// Generated from elaborated C++HDL IC purposes; all contributions coexist.\n"
        << "// Capacitor initialization belongs to the host analysis; declared initial voltages appear below.\n"
        << "`include \"disciplines.vams\"\n\nmodule " << name << '(';
    bool comma = false;
    for (const auto& p : ports) {
        if (comma)
            out << ", ";
        out << p.name;
        comma = true;
    }
    std::set<std::size_t> driver_bits;
    if (experiment)
        for (const auto& e : sim.elements)
            if (e.kind == law::sim_driver)
                driver_bits.insert(e.bit);
    for (auto bit : driver_bits)
        out << ", cpphdl_bit" << bit;
    out << ");\n    inout ";
    comma = false;
    for (const auto& p : ports) {
        if (comma)
            out << ", ";
        out << p.name;
        comma = true;
    }
    for (auto bit : driver_bits)
        out << ", cpphdl_bit" << bit;
    out << ";\n    electrical ";
    comma = false;
    for (const auto& p : ports) {
        if (comma)
            out << ", ";
        out << p.name;
        comma = true;
    }
    for (auto bit : driver_bits)
        out << ", cpphdl_bit" << bit;
    out << ";\n";
    for (const auto& [id, n] : names)
        if (!used_names.contains(n))
            out << "    electrical " << n << ";\n";
    bool has_mos = std::any_of(sim.elements.begin(), sim.elements.end(),
                               [](const auto& e) { return e.kind == law::nmos || e.kind == law::pmos; });
    if (has_mos)
        out << mos_function;
    out << "\n    analog begin\n";
    for (const auto& [alias, primary] : aliases)
        out << "        V(" << alias << ", " << primary << ") <+ 0.0; // Preserve aliased external ports.\n";
    for (std::size_t index = 0; index < sim.elements.size(); ++index) {
        const auto& e = sim.elements[index];
        if (experiment && e.kind == law::sim_voltage_source)
            continue; // Ideal bench voltage sources belong in the SPICE deck.
        if (experiment && e.kind == law::capacitor && spice_capacitance_threshold > 0 &&
            e.c >= spice_capacitance_threshold) {
            out << "        // Capacitor device " << index << " is stamped once by the SPICE host deck.\n";
            continue;
        }
        out << "        // device " << index;
        if (e.component != no_component)
            out << ", IC " << comment(sim.components().at(e.component).name);
        out << ", " << comment(e.file) << ':' << e.line << "\n";
        auto ab = e.kind == law::sim_driver ? std::string{} : branch(e.a, e.b);
        switch (e.kind) {
        case law::resistor:
            out << "        I" << ab << " <+ V" << ab << " / " << number(e.r) << ";\n";
            break;
        case law::capacitor:
            out << "        I" << ab << " <+ " << number(e.c) << " * ddt(V" << ab << ");"
                << " // Bench initial voltage = " << number(e.initial) << " V.\n";
            break;
        case law::inductor:
            out << "        V" << ab << " <+ " << number(e.l) << " * ddt(I" << ab << ");"
                << " // Initial branch current = " << number(e.initial) << " A.\n";
            break;
        case law::transconductor:
            out << "        I" << ab << " <+ (" << number(-e.gm) << ") * V" << branch(e.gate, e.reference) << ";\n";
            break;
        case law::diode:
            out << "        I" << ab << " <+ max(0.0, V" << ab << " - " << number(e.threshold) << ") / " << number(e.r)
                << " + V" << ab << " / " << number(e.value) << ";\n";
            break;
        case law::nmos:
        case law::pmos:
            out << "        I" << ab << " <+ cpphdl_channel(V" << branch(e.gate, e.b) << ", V" << ab << ", "
                << number(e.beta) << ", " << number(e.threshold) << ", " << (e.kind == law::pmos ? "-1.0" : "1.0")
                << ");\n";
            break;
        case law::sim_controlled_resistor: {
            auto fraction = "min(1.0, max(0.0, (V" + branch(e.gate, e.reference) + " - " + number(e.low) + ") / " +
                            number(e.high - e.low) + "))";
            if (e.falling)
                fraction = "(1.0 - " + fraction + ")";
            out << "        I" << (e.buffered ? branch(e.a, e.reference) : ab) << " <+ " << fraction << " * V" << ab
                << " / " << number(e.r)
                << (e.buffered ? "; // sim_buffered_track\n" : "; // sim_controlled_resistor\n");
            break;
        }
        case law::sim_driver: {
            std::string bit = "V(cpphdl_bit" + std::to_string(e.bit) + ")";
            for (bool high : {false, true}) {
                auto path = branch(e.a, high ? e.vdd : e.vss);
                out << "        I" << path << " <+ " << (high ? bit : "(1.0 - " + bit + ")") << " * V" << path << " / "
                    << number(e.r) << "; // sim_driver\n";
            }
            break;
        }
        case law::sim_voltage_source:
            fail("voltage stimulus cannot be embedded in a reusable IC");
        }
    }
    out << "    end\nendmodule\n";
    return out.str();
}
inline void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path);
    if (!file || !(file << contents))
        throw diagnostic("E_IO", "cannot write " + path.string());
}
} // namespace detail

inline std::string render_module(const simulation& sim, const std::string& name, const std::vector<named_port>& ports) {
    std::vector<detail::node_port> bindings;
    for (const auto& p : ports) {
        sim.check(p.terminal, p.terminal);
        bindings.push_back({p.name, p.terminal.id()});
    }
    return detail::module(sim, name, bindings, false);
}
inline void write_module(const IC& component, const std::filesystem::path& path, const std::string& name,
                         const std::vector<named_port>& ports) {
    // Use a dedicated elaboration context: every physical relation in that context is exported.
    auto text = render_module(component.owner(), name, ports);
    detail::write_file(path, text);
}

// Explicitly select a behavioral approximation without pretending it is a transistor implementation.
inline std::string render_simulation_model(const simulation& sim, const std::string& name,
                                           const std::vector<named_port>& ports) {
    for (const auto& e : sim.elements)
        if (e.kind == law::sim_voltage_source || e.kind == law::sim_driver)
            detail::fail("simulation model export accepts controlled resistors, not bench sources or bit drivers");
    std::vector<detail::node_port> bindings;
    for (const auto& p : ports) {
        sim.check(p.terminal, p.terminal);
        bindings.push_back({p.name, p.terminal.id()});
    }
    return "// SIMULATION APPROXIMATION: controlled conductances are not physical MOS devices.\n" +
           detail::module(sim, name, bindings, true);
}

// Cut a mixed bench at its ideal bridges. The host must supply every source and driver control.
// This is deliberately separate from full export: digital operations are outside this partition.
inline std::string render_analog_partition(const simulation& sim, const std::string& name,
                                           const std::vector<named_port>& ports) {
    std::vector<detail::node_port> bindings;
    std::set<std::size_t> exposed;
    for (const auto& p : ports) {
        sim.check(p.terminal, p.terminal);
        bindings.push_back({p.name, p.terminal.id()});
        exposed.insert(sim.root(p.terminal.id()));
    }
    for (const auto& e : sim.elements)
        if (e.kind == law::sim_voltage_source && (!exposed.contains(sim.root(e.a)) || !exposed.contains(sim.root(e.b))))
            detail::fail("analog partition must expose both terminals of every external bench source");
    return "// ANALOG PARTITION ONLY: no digital computation, sensing or register state is included.\n"
           "// Supply omitted bench sources at the named ports. cpphdl_bit<ID> controls use 0/1 V relative to ground.\n"
           "// Each bit port must be driven by the host digital controller; an unconnected bit port is invalid.\n" +
           detail::module(sim, name, bindings, true, true);
}

} // namespace analog::verilog_a
