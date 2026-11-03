#include <behavior_devices.hpp>
#include <chrono>
#include <iostream>

namespace b = analog::behavior;

// Instrument the actual class dispatch without changing its equations.
class counted_linear : public b::dense_solver {
public:
    std::size_t calls = 0;
    std::vector<double> solve(std::size_t n, const std::vector<analog::matrix_entry>& a,
                              const std::vector<double>& rhs) override {
        ++calls;
        return b::dense_solver::solve(n, a, rhs);
    }
};

class nonlinear_storage;
class nonlinear_load;
class sim_current_ramp;
namespace analog::behavior {
template <> struct traits<::nonlinear_storage> : traits<capacitor> {
    static constexpr std::string_view name = "cubic charge regression";
};
template <> struct traits<::nonlinear_load> : traits<resistor> {
    static constexpr std::string_view name = "cubic current regression";
};
template <> struct traits<::sim_current_ramp> : traits<device> {
    static constexpr capabilities features{.breakpoints = true};
    static constexpr std::string_view name = "simulation current ramp";
};
}

class nonlinear_storage : public b::typed_device<nonlinear_storage, b::capacitor> {
public:
    void evaluate(const b::evaluation& state, b::equations& out) override {
        const double v = state.value(p_) - state.value(n_);
        out.branch(p_, n_, 0, capacitance.value * (v + v * v * v),
                   0, capacitance.value * (1 + 3 * v * v));
    }
};

class nonlinear_load : public b::typed_device<nonlinear_load, b::resistor> {
public:
    void evaluate(const b::evaluation& state, b::equations& out) override {
        const double v = state.value(p_) - state.value(n_);
        out.branch(p_, n_, (v + v * v * v) / resistance.value, 0,
                   (1 + 3 * v * v) / resistance.value, 0);
    }
};

class sim_current_ramp : public b::typed_device<sim_current_ramp, b::two_terminal> {
public:
    void evaluate(const b::evaluation& state, b::equations& out) override {
        out.branch(p_, n_, -.001 * std::min(1.0, state.time / 1e-7), 0, 0, 0);
    }
    double next_breakpoint(double time) const override {
        return time < 1e-7 ? 1e-7 : std::numeric_limits<double>::infinity();
    }
};

// Test circuits are ICs. Derived circuits retain ports and specialize behavior.
class workload : public analog::IC {
public:
    _PORT(analog::electrical) OUT;
    double stop = 2e-6, step = 1e-9;
    virtual std::string reference() const = 0;
    virtual std::vector<double> initial(b::network&) const { return {}; }
    virtual std::vector<analog::electrical> observations() const { return {OUT}; }
};

class decay : public workload {
public:
    b::resistor load;
    b::capacitor storage;
    void _assign() override {
        load.P = OUT;
        load.N = Vss;
        storage.P = OUT;
        storage.N = Vss;
        storage.capacitance = analog::capacitance_value(1e-9);
        load.elaborate();
        storage.elaborate();
    }
    std::vector<double> initial(b::network& net) const override {
        std::vector<double> x(net.size());
        x[net.index(OUT)] = 1;
        return x;
    }
    std::string reference() const override { return "Rload out 0 1k\nCstore out 0 1n IC=1\n"; }
};

class nonlinear_charging : public workload {
public:
    b::resistor load;
    nonlinear_storage storage;
    sim_current_ramp stimulus;
    void _assign() override {
        load.P = OUT;
        load.N = Vss;
        storage.P = OUT;
        storage.N = Vss;
        storage.capacitance = analog::capacitance_value(1e-9);
        stimulus.P = OUT;
        stimulus.N = Vss;
        load.elaborate();
        storage.elaborate();
        stimulus.elaborate();
    }
    std::string reference() const override {
        return "Rload out 0 1k\nIinput 0 out PWL(0 0 100n 1m)\n"
               "Cstore out 0 Q='1n*(v(out)+v(out)^3)'\n";
    }
};

class coupled_decay : public decay {
public:
    _PORT(analog::electrical) OTHER;
    b::resistor other_load;
    b::capacitor other_storage, coupling;
    void _assign() override {
        decay::_assign();
        other_load.P = OTHER;
        other_load.N = Vss;
        other_storage.P = OTHER;
        other_storage.N = Vss;
        coupling.P = OUT;
        coupling.N = OTHER;
        other_storage.capacitance = analog::capacitance_value(1e-9);
        coupling.capacitance = analog::capacitance_value(.5e-9);
        other_load.elaborate();
        other_storage.elaborate();
        coupling.elaborate();
    }
    std::vector<analog::electrical> observations() const override { return {OUT, OTHER}; }
    std::string reference() const override {
        return decay::reference() + "Rother other 0 1k\nCother other 0 1n IC=0\n"
               "Ccoupling out other .5n IC=1\n";
    }
};

class ramp_filter : public decay {
public:
    b::sim_pwl_voltage_source stimulus;
    b::resistor feed;
    void _assign() override {
        decay::_assign();
        stimulus.N = Vss;
        stimulus.points = {{0, 0}, {2e-7, 1}, {8e-7, 1}, {1e-6, 0}};
        feed.P = stimulus.P;
        feed.N = OUT;
        stimulus.elaborate();
        feed.elaborate();
    }
    std::vector<double> initial(b::network&) const override { return {}; }
    std::string reference() const override {
        return "Vin input 0 PWL(0 0 .2u 1 .8u 1 1u 0)\nRfeed input out 1k\n"
               "Rload out 0 1k\nCstore out 0 1n IC=0\n";
    }
};

class divider : public workload {
public:
    b::sim_voltage_source supply;
    b::resistor feed, load;
    double input = 1;
    void _assign() override {
        supply.N = Vss;
        supply.level = analog::voltage(input);
        feed.P = supply.P;
        feed.N = OUT;
        load.P = OUT;
        load.N = Vss;
        supply.elaborate();
        feed.elaborate();
        load.elaborate();
        stop = 1e-9;
        step = 1e-10;
    }
    std::string reference() const override {
        return "Vin input 0 " + std::to_string(input) + "\nRfeed input out 1k\nRload out 0 1k\n";
    }
};

class nonlinear_divider : public workload {
public:
    b::sim_voltage_source supply;
    b::resistor feed;
    nonlinear_load load;
    double input = 1;
    void _assign() override {
        supply.N = Vss;
        supply.level = analog::voltage(input);
        feed.P = supply.P;
        feed.N = OUT;
        load.P = OUT;
        load.N = Vss;
        supply.elaborate();
        feed.elaborate();
        load.elaborate();
        stop = 1e-9;
        step = 1e-10;
    }
    std::string reference() const override {
        return "Vin input 0 " + std::to_string(input) + "\nRfeed input out 1k\n"
               "Bload out 0 I=(v(out)+v(out)^3)/1000\n";
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("usage: bench CASE OUTPUT REFINEMENT INPUT");
        analog::simulation context;
        std::unique_ptr<workload> dut;
        const std::string name = argv[1];
        const double input = std::stod(argv[4]);
        if (!std::isfinite(input) || input < -1 || input > 1) throw std::runtime_error("invalid input");
        if (name == "linear_dc") {
            auto model = std::make_unique<divider>();
            model->input = input;
            dut = std::move(model);
        } else if (name == "nonlinear_dc") {
            auto model = std::make_unique<nonlinear_divider>();
            model->input = input;
            dut = std::move(model);
        } else if (name == "linear_charge") dut = std::make_unique<decay>();
        else if (name == "nonlinear_charge") dut = std::make_unique<nonlinear_charging>();
        else if (name == "coupled_rc") dut = std::make_unique<coupled_decay>();
        else if (name == "pwl_breakpoints") dut = std::make_unique<ramp_filter>();
        else throw std::runtime_error("unknown workload");
        context.reference(dut->Vss);
        dut->elaborate();
        const int refinement = std::stoi(argv[3]);
        if (refinement != 1 && refinement != 2) throw std::runtime_error("invalid refinement");
        const double step = dut->step / refinement;
        const bool dc = name == "linear_dc" || name == "nonlinear_dc";
        b::network net(context);
        net.compile();
        b::coupled_solver solver;
        auto linear = std::make_shared<counted_linear>();
        solver.linear = linear;
        b::fixed_step steps(step);
        std::filesystem::path work = argv[2];
        std::filesystem::create_directories(work);
        const auto start = std::chrono::steady_clock::now();
        std::vector<b::point> trace;
        if (dc) {
            std::vector<double> zero(net.size());
            net.begin(0, 0);
            try {
                auto x = solver.nonlinear->solve(net, 0, zero, {0, 0, 0}, zero, zero, *linear);
                net.accept();
                trace.push_back({0, std::move(x)});
            } catch (...) { net.reject(); throw; }
        } else {
            trace = solver.run(net, dut->stop, steps, dut->initial(net));
        }
        if (trace.size() > 4100) throw std::runtime_error("smoke point budget exceeded");
        auto observations = dut->observations();
        {
            std::ofstream csv(work / "cpp.csv");
            csv << std::setprecision(17) << "time_s,out";
            if (observations.size() == 2) csv << ",other";
            csv << '\n';
            for (const auto& point : trace) {
                csv << point.time;
                for (const auto& wire : observations) csv << ',' << point.solution[net.index(wire)];
                csv << '\n';
            }
            if (!csv) throw std::runtime_error("native trace write failed");
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::ofstream spice(work / "reference.cir");
        spice << std::setprecision(17) << "Independent workload reference\n" << dut->reference()
              << ".options method=gear maxord=2 reltol=1e-8 vntol=1e-10 abstol=1e-13\n"
              << ".control\nset numdgt=17\nset wr_singlescale\nset wr_vecnames\n";
        if (dc) spice << "op\n";
        else spice << "tran " << step << ' ' << dut->stop << " 0 " << step
                   << (dut->initial(net).empty() ? "\n" : " uic\n");
        spice << "wrdata reference.dat v(out)" << (observations.size() == 2 ? " v(other)" : "")
              << "\nquit\n.endc\n.end\n";
        std::ofstream metadata(work / "native.json");
        metadata << std::setprecision(17) << "{\"engine\":\"cpphdl\",\"points\":" << trace.size()
                 << ",\"linear_solves\":" << linear->calls << ",\"unknowns\":" << net.size()
                 << ",\"stop_s\":" << (dc ? 0 : dut->stop) << ",\"step_s\":" << (dc ? 0 : step)
                 << ",\"simulation_and_output_wall_s\":" << elapsed << "}\n";
        if (!spice || !metadata) throw std::runtime_error("workload metadata write failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
