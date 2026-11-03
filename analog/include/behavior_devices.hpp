#pragma once
#include "behavior.hpp"

namespace analog::behavior {

class resistor;
class capacitor;
class sim_voltage_source;
class sim_pwl_voltage_source;

template <> struct traits<resistor> : traits<device> {
    static constexpr std::string_view name = "linear resistor";
};
template <> struct traits<capacitor> : traits<device> {
    static constexpr capabilities features{.charge = true};
    static constexpr std::string_view name = "linear capacitor";
};
template <> struct traits<sim_voltage_source> : traits<device> {
    static constexpr std::string_view name = "simulation voltage source";
};
template <> struct traits<sim_pwl_voltage_source> : traits<sim_voltage_source> {
    static constexpr capabilities features{.breakpoints = true};
    static constexpr std::string_view name = "simulation PWL voltage source";
};

class two_terminal : public device {
protected:
    std::size_t p_ = ground, n_ = ground;
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    void bind(binding& layout) override {
        p_ = layout.node(P);
        n_ = layout.node(N);
    }
};

class resistor : public typed_device<resistor, two_terminal> {
public:
    analog::resistance resistance{1000};
    void bind(binding& layout) override {
        positive(resistance.value, "behavior resistance");
        two_terminal::bind(layout);
    }
    void evaluate(const evaluation& state, equations& result) override {
        const double g = 1 / resistance.value;
        result.branch(p_, n_, (state.value(p_) - state.value(n_)) * g, 0, g, 0);
    }
};

class capacitor : public typed_device<capacitor, two_terminal> {
public:
    capacitance_value capacitance{1e-12};
    void bind(binding& layout) override {
        positive(capacitance.value, "behavior capacitance");
        two_terminal::bind(layout);
    }
    void evaluate(const evaluation& state, equations& result) override {
        result.branch(p_, n_, 0, (state.value(p_) - state.value(n_)) * capacitance.value,
                      0, capacitance.value);
    }
};

// A simulation stimulus. Hardware sources should be built from physical devices.
class sim_voltage_source : public typed_device<sim_voltage_source, two_terminal> {
    std::size_t branch_ = ground;
public:
    analog::voltage level{0};
    virtual double value(double) const { return level.value; }
    void bind(binding& layout) override {
        finite(level.value, "behavior source voltage");
        two_terminal::bind(layout);
        branch_ = layout.branch();
    }
    void evaluate(const evaluation& state, equations& result) override {
        result.add(p_, state.value(branch_));
        result.add(n_, -state.value(branch_));
        result.add(branch_, state.value(p_) - state.value(n_) - value(state.time));
        result.derivative(p_, branch_, 1);
        result.derivative(n_, branch_, -1);
        result.derivative(branch_, p_, 1);
        result.derivative(branch_, n_, -1);
    }
};

class sim_pwl_voltage_source : public typed_device<sim_pwl_voltage_source, sim_voltage_source> {
public:
    std::vector<std::pair<double, double>> points{{0, 0}};
    void bind(binding& layout) override {
        if (points.empty() || points.front().first != 0)
            throw diagnostic("E_VALUE", "behavior PWL must start at time zero");
        double previous = -1;
        for (const auto& [time, level] : points) {
            if (!std::isfinite(time) || !std::isfinite(level) || time <= previous)
                throw diagnostic("E_VALUE", "behavior PWL needs finite values and increasing times");
            previous = time;
        }
        sim_voltage_source::bind(layout);
    }
    auto following(double time) const {
        return std::upper_bound(points.begin(), points.end(), time,
            [](double t, const auto& point) { return t < point.first; });
    }
    double value(double time) const override {
        const auto next = following(time);
        if (next == points.begin()) return next->second;
        const auto before = std::prev(next);
        if (next == points.end()) return before->second;
        const double fraction = (time - before->first) / (next->first - before->first);
        return before->second + fraction * (next->second - before->second);
    }
    double next_breakpoint(double time) const override {
        const auto next = following(time);
        return next == points.end() ? std::numeric_limits<double>::infinity() : next->first;
    }
};

} // namespace analog::behavior
