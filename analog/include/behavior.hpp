#pragma once
#include "analog.hpp"
#include <memory>
#include <span>
#include <string_view>

// An opt-in class-based backend. It shares IC identity and electrical connections
// with analog.hpp; it does not change the existing solver or its saved references.
namespace analog::behavior {

inline constexpr std::size_t ground = no_component;

struct capabilities {
    bool charge = false;
    bool breakpoints = false;
    bool rollback = false;
    bool compact_model = false;
};

// Specializations inherit their parent's traits and change only the differences.
template <class T> struct traits {
    static constexpr capabilities features{};
    static constexpr std::string_view name = "unspecified behavior";
};

class binding {
    analog::simulation& owner_;
    std::map<std::size_t, std::size_t> nodes_;

public:
    std::vector<bool> voltage_equation;
    explicit binding(analog::simulation& owner) : owner_(owner) {}
    std::size_t node(const electrical& wire) {
        if (&wire.owner() != &owner_)
            throw diagnostic("E_CONTEXT", "behavior port belongs to another simulation");
        const auto root = owner_.root(wire.id());
        if (root == 0)
            return ground;
        if (!nodes_.contains(root)) {
            nodes_.emplace(root, voltage_equation.size());
            voltage_equation.push_back(false); // KCL row, in amperes.
        }
        return nodes_.at(root);
    }
    std::size_t branch() {
        voltage_equation.push_back(true); // Ideal voltage constraint, in volts.
        return voltage_equation.size() - 1;
    }
    const auto& nodes() const { return nodes_; }
};

struct evaluation {
    double time;
    std::span<const double> solution;
    double value(std::size_t index) const {
        if (index == ground) return 0;
        if (index >= solution.size()) throw diagnostic("E_BEHAVIOR", "invalid model unknown index");
        return solution[index];
    }
};

// Every object contributes to F(x,t) + dQ(x,t)/dt = 0. Contributions add;
// connected objects never overwrite one another's outputs.
struct equations {
    std::vector<double> current, charge;
    std::vector<matrix_entry> conductance, capacitance;
    explicit equations(std::size_t n) : current(n), charge(n) {}
    void add(std::size_t row, double f, double q = 0) {
        if (row != ground) {
            current.at(row) += f;
            charge.at(row) += q;
        }
    }
    void derivative(std::size_t row, std::size_t column, double g, double c = 0) {
        if (row != ground && column != ground) {
            if (g != 0) conductance.push_back({row, column, g});
            if (c != 0) capacitance.push_back({row, column, c});
        }
    }
    void branch(std::size_t p, std::size_t n, double i, double q, double g, double c) {
        add(p, i, q);
        add(n, -i, -q);
        derivative(p, p, g, c);
        derivative(p, n, -g, -c);
        derivative(n, p, -g, -c);
        derivative(n, n, g, c);
    }
};

class device : public IC {
    struct registry {
        std::vector<device*> objects;
        std::uint64_t revision = 0;
    };
    inline static thread_local std::map<analog::simulation*, registry> registries_;
    inline static thread_local std::uint64_t revision_ = 0;
    friend class network;
public:
    device() {
        auto& registry = registries_[&owner()];
        registry.objects.push_back(this);
        registry.revision = ++revision_;
    }
    ~device() override {
        auto& registry = registries_.at(&owner());
        std::erase(registry.objects, this);
        registry.revision = ++revision_;
        if (registry.objects.empty()) registries_.erase(&owner());
    }
    void _assign() override {}
    virtual capabilities features() const = 0;
    virtual void bind(binding&) = 0;
    virtual void evaluate(const evaluation&, equations&) = 0;
    virtual double next_breakpoint(double) const { return std::numeric_limits<double>::infinity(); }
    // A trial may contain many Newton evaluations. Only accept commits state.
    // Stateful subclasses must implement all three methods, including rollback.
    virtual void begin_step(double, double) {
        if (features().rollback)
            throw diagnostic("E_BEHAVIOR", "stateful behavior must implement begin_step");
    }
    virtual void accept() noexcept {}
    virtual void reject() noexcept {}
};

class stateful_device : public device {
public:
    void begin_step(double, double) override = 0;
    void accept() noexcept override = 0;
    void reject() noexcept override = 0;
};

template <class Derived, class Base = device> class typed_device : public Base {
public:
    capabilities features() const override { return traits<Derived>::features; }
};

class network {
    analog::simulation& owner_;
    std::vector<device*> devices_;
    binding binding_;
    std::map<std::size_t, std::size_t> roots_;
    bool compiled_ = false;
    std::size_t active_ = 0;
    std::uint64_t revision_ = 0;

public:
    explicit network(analog::simulation& owner) : owner_(owner), binding_(owner) {}
    network(const network&) = delete;
    network& operator=(const network&) = delete;
    ~network() { reject(); }
    void add(device& item) {
        if (compiled_) throw diagnostic("E_ELABORATION", "behavior network is already bound");
        if (&item.owner() != &owner_) throw diagnostic("E_CONTEXT", "behavior belongs to another simulation");
        if (std::find(devices_.begin(), devices_.end(), &item) != devices_.end())
            throw diagnostic("E_BEHAVIOR", "behavior registered twice");
        devices_.push_back(&item);
    }
    void compile() {
        if (compiled_) { validate(); return; }
        if (!owner_.elements.empty() || !owner_.digital.empty() || !owner_.operations.empty())
            throw diagnostic("E_UNSUPPORTED", "behavior backend does not yet lower legacy elements or digital operations");
        for (const auto& item : owner_.components())
            if (item.state != component_state::ready)
                throw diagnostic("E_ELABORATION", "elaborate every IC before binding its behavior");
        auto registry = device::registries_.find(&owner_);
        for (auto* item : devices_)
            if (registry == device::registries_.end() ||
                std::find(registry->second.objects.begin(), registry->second.objects.end(), item) == registry->second.objects.end())
                throw diagnostic("E_ELABORATION", "registered device was destroyed before behavior binding");
        if (registry != device::registries_.end()) {
            for (auto* item : registry->second.objects)
                if (std::find(devices_.begin(), devices_.end(), item) == devices_.end()) devices_.push_back(item);
            revision_ = registry->second.revision;
        }
        for (auto* item : devices_) item->bind(binding_);
        for (const auto& [wire, index] : binding_.nodes()) {
            (void) index;
            roots_.emplace(wire, owner_.root(wire));
        }
        compiled_ = true;
    }
    void validate() const {
        const auto registry = device::registries_.find(&owner_);
        const auto revision = registry == device::registries_.end() ? 0 : registry->second.revision;
        if (revision != revision_)
            throw diagnostic("E_ELABORATION", "device lifetime or membership changed after behavior binding");
        for (const auto& [wire, root] : roots_)
            if (owner_.root(wire) != root)
                throw diagnostic("E_ELABORATION", "electrical connections changed after behavior binding");
        if (!owner_.elements.empty() || !owner_.digital.empty() || !owner_.operations.empty())
            throw diagnostic("E_UNSUPPORTED", "legacy behavior added after binding");
    }
    std::size_t size() const { return binding_.voltage_equation.size(); }
    bool voltage_row(std::size_t i) const { return binding_.voltage_equation.at(i); }
    std::size_t index(const electrical& wire) const {
        if (&wire.owner() != &owner_) throw diagnostic("E_CONTEXT", "observation belongs to another simulation");
        const auto root = owner_.root(wire.id());
        return root == 0 ? ground : binding_.nodes().at(root);
    }
    equations evaluate(double time, const std::vector<double>& x) {
        validate();
        if (x.size() != size()) throw diagnostic("E_BEHAVIOR", "wrong solution dimension");
        equations result(size());
        for (auto* item : devices_) item->evaluate({time, x}, result);
        for (const auto* values : {&result.current, &result.charge})
            for (double value : *values)
                if (!std::isfinite(value)) throw diagnostic("E_BEHAVIOR", "nonfinite current or charge");
        return result;
    }
    double next_breakpoint(double time) const {
        double next = std::numeric_limits<double>::infinity();
        for (auto* item : devices_) {
            const double point = item->next_breakpoint(time);
            if (std::isnan(point) || point <= time)
                throw diagnostic("E_BEHAVIOR", "breakpoint must be strictly after accepted time");
            next = std::min(next, point);
        }
        return next;
    }
    void begin(double from, double to) {
        if (active_) throw diagnostic("E_BEHAVIOR", "nested behavior trial");
        try {
            for (auto* item : devices_) {
                ++active_; // Include a partially initialized object in rollback.
                item->begin_step(from, to);
            }
        } catch (...) { reject(); throw; }
    }
    void accept() noexcept {
        for (std::size_t i = 0; i < active_; ++i) devices_[i]->accept();
        active_ = 0;
    }
    void reject() noexcept {
        while (active_) {
            auto* item = devices_[--active_];
            const auto registry = device::registries_.find(&owner_);
            if (registry != device::registries_.end() &&
                std::find(registry->second.objects.begin(), registry->second.objects.end(), item) != registry->second.objects.end())
                item->reject();
        }
    }
};

class linear_solver {
public:
    virtual ~linear_solver() = default;
    virtual std::vector<double> solve(std::size_t, const std::vector<matrix_entry>&,
                                      const std::vector<double>&) = 0;
};

class dense_solver : public linear_solver {
public:
    std::vector<double> solve(std::size_t n, const std::vector<matrix_entry>& entries,
                              const std::vector<double>& rhs) override {
        std::vector<std::vector<double>> matrix(n, std::vector<double>(n));
        for (const auto& entry : entries) matrix.at(entry.row).at(entry.column) += entry.value;
        return analog::linear_solve(std::move(matrix), rhs);
    }
};

// Reuses the existing Eigen backend or another legacy backend without changing
// equations, model choice, integration or convergence policy.
class backend_solver : public linear_solver {
    linear_system_backend backend_;
public:
    explicit backend_solver(linear_system_backend backend) : backend_(std::move(backend)) {
        if (!backend_) throw diagnostic("E_VALUE", "missing linear backend");
    }
    std::vector<double> solve(std::size_t n, const std::vector<matrix_entry>& entries,
                              const std::vector<double>& rhs) override {
        return backend_(n, entries, rhs);
    }
};

struct weights { double now, previous, older; };
class integration {
public:
    virtual ~integration() = default;
    virtual weights coefficients(double step, double previous_step, bool restart) const = 0;
};
class backward_euler : public integration {
public:
    weights coefficients(double step, double, bool) const override { return {1 / step, -1 / step, 0}; }
};
class bdf2 : public backward_euler {
public:
    weights coefficients(double step, double previous, bool restart) const override {
        if (restart || previous <= 0 || step > 2 * previous)
            return backward_euler::coefficients(step, previous, restart);
        const double r = step / previous;
        return {(1 + 2 * r) / ((1 + r) * step), -(1 + r) / step, r * r / ((1 + r) * step)};
    }
};

struct tolerances {
    double relative = 1e-7;
    double voltage = 1e-9;
    double current = 1e-12;
    unsigned iterations = 60;
};

class nonlinear_solver {
public:
    virtual ~nonlinear_solver() = default;
    virtual std::vector<double> solve(network&, double, std::vector<double>, weights,
        const std::vector<double>&, const std::vector<double>&, linear_solver&) = 0;
};

class damped_newton : public nonlinear_solver {
public:
    tolerances tolerance{};
    std::vector<double> solve(network& net, double time, std::vector<double> x, weights w,
        const std::vector<double>& previous, const std::vector<double>& older, linear_solver& linear) override {
        if (!(tolerance.relative > 0 && tolerance.voltage > 0 && tolerance.current > 0) ||
            !std::isfinite(tolerance.relative + tolerance.voltage + tolerance.current))
            throw diagnostic("E_VALUE", "invalid Newton tolerances");
        auto residual = [&](const equations& eq) {
            auto f = eq.current;
            for (std::size_t i = 0; i < f.size(); ++i)
                f[i] += w.now * eq.charge[i] + w.previous * previous[i] + w.older * older[i];
            return f;
        };
        auto norm = [&](const std::vector<double>& f) {
            double result = 0;
            for (std::size_t i = 0; i < f.size(); ++i) {
                if (!std::isfinite(f[i])) return std::numeric_limits<double>::infinity();
                result = std::max(result, std::abs(f[i]) /
                    (net.voltage_row(i) ? tolerance.voltage : tolerance.current));
            }
            return result;
        };
        for (unsigned iteration = 0; iteration < tolerance.iterations; ++iteration) {
            const auto eq = net.evaluate(time, x);
            auto f = residual(eq);
            const double before = norm(f);
            auto matrix = eq.conductance;
            for (auto entry : eq.capacitance) {
                entry.value *= w.now;
                if (entry.value != 0) matrix.push_back(entry);
            }
            for (const auto& entry : matrix)
                if (entry.row >= x.size() || entry.column >= x.size() || !std::isfinite(entry.value))
                    throw diagnostic("E_BEHAVIOR", "invalid model Jacobian");
            for (auto& value : f) value = -value;
            const auto delta = linear.solve(x.size(), matrix, f);
            if (delta.size() != x.size()) throw diagnostic("E_LINEAR_SOLVER", "incomplete Newton direction");
            for (auto value : delta)
                if (!std::isfinite(value)) throw diagnostic("E_LINEAR_SOLVER", "nonfinite Newton direction");
            // Check the original assembled equations even for custom solvers.
            auto error = f;
            std::vector<double> scale(f.size());
            for (std::size_t i = 0; i < f.size(); ++i) scale[i] = std::abs(f[i]);
            for (const auto& entry : matrix) {
                const double term = entry.value * delta[entry.column];
                error[entry.row] -= term;
                scale[entry.row] += std::abs(term);
            }
            for (std::size_t i = 0; i < f.size(); ++i)
                if (!std::isfinite(error[i]) || !std::isfinite(scale[i]) ||
                    std::abs(error[i]) > (net.voltage_row(i) ? tolerance.voltage : tolerance.current) +
                        tolerance.relative * scale[i])
                    throw diagnostic("E_LINEAR_ACCURACY", "custom solver failed the assembled residual check");
            if (before <= 1) return x; // Rank was still checked by the linear solver.
            bool accepted = false;
            double alpha = 1;
            for (unsigned trial = 0; trial < 24; ++trial, alpha *= .5) {
                auto next = x;
                for (std::size_t i = 0; i < x.size(); ++i) next[i] += alpha * delta[i];
                if (norm(residual(net.evaluate(time, next))) < before) {
                    x = std::move(next);
                    accepted = true;
                    break;
                }
            }
            if (!accepted) throw diagnostic("E_NONCONVERGENCE", "behavior Newton line search failed");
        }
        throw diagnostic("E_NONCONVERGENCE", "behavior Newton iteration limit");
    }
};

class step_control {
public:
    virtual ~step_control() = default;
    virtual double proposed(double time) const = 0;
    virtual double retry(double failed) const = 0;
    virtual void accepted(double, double) {}
};
class fixed_step : public step_control {
    double step_, minimum_;
public:
    explicit fixed_step(double step, double minimum = 0) : step_(step), minimum_(minimum) {
        if (!std::isfinite(step) || step <= 0 || !std::isfinite(minimum) || minimum < 0 || minimum > step)
            throw diagnostic("E_VALUE", "invalid behavior step bounds");
    }
    double proposed(double) const override { return step_; }
    double retry(double failed) const override {
        if (minimum_ == 0 || failed * .5 < minimum_)
            throw diagnostic("E_TIMESTEP", "behavior step failed at minimum interval");
        return failed * .5;
    }
};

struct point { double time; std::vector<double> solution; };
class transient_solver {
public:
    virtual ~transient_solver() = default;
    virtual std::vector<point> run(network&, double, step_control&, std::vector<double> initial = {}) = 0;
};

class coupled_solver : public transient_solver {
public:
    std::shared_ptr<linear_solver> linear = std::make_shared<dense_solver>();
    std::shared_ptr<nonlinear_solver> nonlinear = std::make_shared<damped_newton>();
    std::shared_ptr<integration> integrator = std::make_shared<bdf2>();

    std::vector<point> run(network& net, double stop, step_control& steps,
                            std::vector<double> initial = {}) override {
        if (!std::isfinite(stop) || stop <= 0 || !linear || !nonlinear || !integrator)
            throw diagnostic("E_VALUE", "invalid behavior simulation configuration");
        net.compile();
        if (net.size() == 0) throw diagnostic("E_UNSUPPORTED", "behavior network has no electrical equations");
        const bool uic = !initial.empty();
        if (!uic) initial.assign(net.size(), 0);
        if (initial.size() != net.size()) throw diagnostic("E_VALUE", "wrong initial state dimension");
        for (auto value : initial)
            if (!std::isfinite(value)) throw diagnostic("E_VALUE", "nonfinite initial state");
        std::vector<double> q(net.size()), older(net.size());
        net.begin(0, 0);
        try {
            if (!uic) initial = nonlinear->solve(net, 0, initial, {0, 0, 0}, q, older, *linear);
            q = net.evaluate(0, initial).charge;
            net.accept();
        } catch (...) { net.reject(); throw; }
        std::vector<point> result{{0, std::move(initial)}};
        double time = 0, previous_step = 0;
        bool restart = true;
        while (time < stop) {
            const double boundary = std::min(stop, net.next_breakpoint(time));
            double dt = std::min(steps.proposed(time), boundary - time);
            // Repeated floating-point additions must not leave a femtoscopic
            // interval immediately before an exact source breakpoint.
            const double roundoff = std::max(64 * std::numeric_limits<double>::epsilon() * std::abs(boundary),
                                             1e-8 * dt);
            if (boundary - time - dt <= roundoff)
                dt = boundary - time;
            for (;;) {
                if (!std::isfinite(dt) || dt <= 0 || time + dt <= time)
                    throw diagnostic("E_TIMESTEP", "behavior simulation cannot advance");
                const double next_time = dt == boundary - time ? boundary : time + dt;
                net.begin(time, next_time);
                std::vector<double> x;
                equations eq(net.size());
                try {
                    x = nonlinear->solve(net, next_time, result.back().solution,
                        integrator->coefficients(dt, previous_step, restart), q, older, *linear);
                    eq = net.evaluate(next_time, x);
                } catch (const diagnostic& error) {
                    net.reject();
                    if (error.code != "E_NONCONVERGENCE" && error.code != "E_LINEAR_ACCURACY") throw;
                    double retry;
                    try { retry = steps.retry(dt); }
                    catch (const diagnostic&) {
                        std::ostringstream detail;
                        detail << std::setprecision(17) << "at t=" << next_time << ", dt=" << dt << ": " << error.what();
                        throw diagnostic("E_TIMESTEP", detail.str());
                    }
                    if (!(retry > 0 && retry < dt)) throw diagnostic("E_TIMESTEP", "retry must reduce the interval");
                    dt = retry;
                    continue;
                } catch (...) { net.reject(); throw; }
                net.accept();
                older = std::move(q);
                q = std::move(eq.charge);
                result.push_back({next_time, std::move(x)});
                restart = next_time == boundary;
                steps.accepted(next_time, dt);
                previous_step = dt;
                time = next_time;
                break;
            }
        }
        return result;
    }
};

} // namespace analog::behavior
