#include <behavior_devices.hpp>
#include <iostream>
#include <type_traits>

namespace b = analog::behavior;

static void require(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}
template <class F> static void rejects(F work, const std::string& code) {
    try { work(); }
    catch (const analog::diagnostic& error) {
        require(error.code == code, error.what());
        return;
    }
    throw std::runtime_error("expected rejection: " + code);
}

class nonlinear_capacitor : public b::capacitor {
public:
    double alpha = 1;
    void evaluate(const b::evaluation& state, b::equations& result) override {
        const double v = state.value(p_) - state.value(n_);
        result.branch(p_, n_, 0, capacitance.value * (v + alpha * v * v * v),
                      0, capacitance.value * (1 + 3 * alpha * v * v));
    }
};
namespace analog::behavior {
template <> struct traits<::nonlinear_capacitor> : traits<capacitor> {
    static constexpr std::string_view name = "nonlinear charge test model";
};
}
static_assert(b::traits<nonlinear_capacitor>::features.charge);
static_assert(std::is_base_of_v<analog::IC, nonlinear_capacitor>);

class filter : public analog::IC {
public:
    _PORT(analog::electrical) Y;
    analog::electrical input;
    b::resistor feed, load;
    b::capacitor storage;
    b::sim_pwl_voltage_source stimulus;
    void _assign() override {
        stimulus.P = input;
        stimulus.N = Vss;
        stimulus.points = {{0, 0}, {2e-7, 1}};
        feed.P = input;
        feed.N = Y;
        load.P = Y;
        load.N = Vss;
        storage.P = Y;
        storage.N = Vss;
        storage.capacitance = analog::capacitance_value(1e-9);
        stimulus.elaborate();
        feed.elaborate();
        load.elaborate();
        storage.elaborate();
    }
};

class counted_solver : public b::dense_solver {
public:
    unsigned calls = 0;
    std::vector<double> solve(std::size_t n, const std::vector<analog::matrix_entry>& entries,
                              const std::vector<double>& rhs) override {
        ++calls;
        return b::dense_solver::solve(n, entries, rhs);
    }
};

static double loaded_filter(double resistance) {
    analog::simulation context;
    filter dut;
    dut.load.resistance = analog::resistance(resistance);
    context.reference(dut.Vss);
    dut.elaborate();
    b::network net(context);
    // The backend discovers all live behavior objects in this simulation.
    auto linear = std::make_shared<counted_solver>();
    b::coupled_solver solver;
    solver.linear = linear;
    b::fixed_step steps(1e-9);
    const auto trace = solver.run(net, 3e-6, steps);
    const auto y = net.index(dut.Y);
    const double gain = resistance / (1000 + resistance);
    const double tau = 1e-9 * 1000 * resistance / (1000 + resistance);
    const double ramp = 2e-7;
    const double at_ramp = gain * (1 - tau / ramp * (1 - std::exp(-ramp / tau)));
    double worst = 0;
    bool breakpoint = false;
    for (const auto& point : trace) {
        const double t = point.time;
        const double exact = t <= ramp ? gain * (t - tau * (1 - std::exp(-t / tau))) / ramp
            : gain + (at_ramp - gain) * std::exp(-(t - ramp) / tau);
        worst = std::max(worst, std::abs(point.solution[y] - exact));
        breakpoint |= t == ramp;
    }
    std::cout << "loaded RC " << resistance << " ohm: maximum error " << worst << " V\n";
    require(worst < 1.2e-5, "loaded RC violated independent analytic solution");
    require(breakpoint && linear->calls > trace.size(), "lost source breakpoint or solver virtual dispatch");
    require(trace.back().time == 3e-6, "incomplete trajectory");
    // Late topology changes must not silently reuse stale unknown indices.
    context.reference(dut.Y);
    rejects([&] { net.evaluate(0, trace.front().solution); }, "E_ELABORATION");
    return trace.back().solution[y];
}

static void nonlinear_charge() {
    analog::simulation context;
    nonlinear_capacitor storage;
    b::resistor decay;
    storage.capacitance = analog::capacitance_value(1e-9);
    decay.P = storage.P;
    decay.N = storage.N;
    context.reference(storage.N);
    storage.elaborate();
    decay.elaborate();
    b::network net(context);
    net.add(storage);
    net.add(decay);
    b::coupled_solver solver;
    b::fixed_step steps(1e-9);
    const auto trace = solver.run(net, 2e-6, steps, {1});
    double worst = 0;
    for (const auto& point : trace) {
        const double v = point.solution[0];
        const double invariant = point.time / 1e-6 + std::log(v) + 1.5 * (v * v - 1);
        worst = std::max(worst, std::abs(invariant));
    }
    std::cout << "nonlinear charge invariant error " << worst << '\n';
    require(worst < 2e-6, "integrated C(V)*V instead of the model's nonlinear Q(V)");
}

class trial_memory : public b::typed_device<trial_memory, b::stateful_device> {
public:
    unsigned committed = 0, rejected = 0, pending = 0;
    void bind(b::binding&) override {}
    void evaluate(const b::evaluation&, b::equations&) override { pending = committed + 1; }
    void begin_step(double, double) override { pending = committed; }
    void accept() noexcept override { committed = pending; }
    void reject() noexcept override { pending = committed; ++rejected; }
};
namespace analog::behavior {
template <> struct traits<::trial_memory> : traits<device> {
    static constexpr capabilities features{.rollback = true};
};
}

class reject_once : public b::damped_newton {
    bool rejected_ = false;
public:
    std::vector<double> solve(b::network& net, double time, std::vector<double> x, b::weights w,
        const std::vector<double>& q, const std::vector<double>& older, b::linear_solver& linear) override {
        auto result = b::damped_newton::solve(net, time, std::move(x), w, q, older, linear);
        if (time > 0 && !rejected_) {
            rejected_ = true;
            throw analog::diagnostic("E_NONCONVERGENCE", "exercise rollback after evaluation");
        }
        return result;
    }
};

class wrong_solver : public b::linear_solver {
public:
    std::vector<double> solve(std::size_t n, const std::vector<analog::matrix_entry>&,
                              const std::vector<double>&) override { return std::vector<double>(n, 0); }
};

static void transactions() {
    analog::simulation context;
    b::resistor decay;
    b::capacitor storage;
    trial_memory state;
    decay.P = storage.P;
    decay.N = storage.N;
    context.reference(decay.N);
    decay.elaborate();
    storage.elaborate();
    state.elaborate();
    require(state.features().rollback, "derived traits were lost during runtime dispatch");
    b::network net(context);
    net.add(decay);
    net.add(storage);
    net.add(state);
    rejects([&] { net.add(state); }, "E_BEHAVIOR");
    b::coupled_solver solver;
    solver.nonlinear = std::make_shared<reject_once>();
    b::fixed_step steps(1e-11, 1e-12);
    auto trace = solver.run(net, 1e-10, steps, {1});
    require(state.rejected == 1, "rejected step did not roll back state");
    require(state.committed == trace.size() && state.pending == state.committed,
            "Newton evaluation committed device state more than once per accepted point");
    // A custom solver is not trusted merely because it returned finite numbers.
    auto wrong = wrong_solver();
    b::damped_newton newton;
    rejects([&] { newton.solve(net, 0, {1}, {0, 0, 0}, {0}, {0}, wrong); }, "E_LINEAR_ACCURACY");
    // Only recoverable numerical errors may trigger timestep retry.
    rejects([&] { solver.run(net, 1e-10, steps, {NAN}); }, "E_VALUE");
}

static void unsupported_and_rank() {
    {
        analog::simulation context;
        auto resistor = std::make_unique<b::resistor>();
        context.reference(resistor->N);
        resistor->elaborate();
        b::network net(context);
        net.compile();
        resistor.reset();
        rejects([&] { net.evaluate(0, {0}); }, "E_ELABORATION");
    }
    {
        analog::simulation context;
        auto resistor = std::make_unique<b::resistor>();
        resistor->elaborate();
        b::network net(context);
        net.add(*resistor);
        resistor.reset();
        rejects([&] { net.compile(); }, "E_ELABORATION");
    }
    {
        analog::simulation context;
        analog::electrical a, g;
        context.reference(g);
        a = analog::resistor(g, analog::resistance(100));
        b::network net(context);
        rejects([&] { net.compile(); }, "E_UNSUPPORTED");
    }
    {
        analog::simulation context;
        b::resistor floating;
        floating.elaborate();
        b::network net(context);
        net.add(floating);
        b::coupled_solver solver;
        b::fixed_step steps(1e-9);
        rejects([&] { solver.run(net, 1e-8, steps); }, "E_SINGULAR");
    }
}

static void picosecond_breakpoints() {
    analog::simulation context;
    b::sim_pwl_voltage_source source;
    b::resistor load;
    source.points = {{0, 0}, {.2e-9, 0}, {.4e-9, 1.2}, {1e-9, 1.2}, {1.2e-9, 0}, {2e-9, 0}};
    load.P = source.P;
    load.N = source.N;
    context.reference(source.N);
    source.elaborate();
    load.elaborate();
    b::network net(context);
    b::coupled_solver solver;
    b::fixed_step steps(5e-13);
    const auto trace = solver.run(net, 2e-9, steps);
    require(trace.size() == 4001, "roundoff inserted an extra breakpoint step");
    for (std::size_t i = 1; i < trace.size(); ++i)
        require(trace[i].time - trace[i - 1].time > 4.99e-13, "roundoff created a tiny timestep");
    for (const auto& [time, voltage] : source.points) {
        auto point = std::find_if(trace.begin(), trace.end(), [&](const auto& p) { return p.time == time; });
        require(point != trace.end(), "source breakpoint was skipped");
        require(std::abs(point->solution[net.index(source.P)] - voltage) < 1e-9, "wrong source breakpoint voltage");
    }
}

int main() {
    try {
        require(loaded_filter(2000) > loaded_filter(1000) + .15, "connected load failed to influence output");
        nonlinear_charge();
        transactions();
        unsupported_and_rank();
        picosecond_breakpoints();
        std::cout << "PASS behavior inheritance, coupled physics, charge, rollback and rejection\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
