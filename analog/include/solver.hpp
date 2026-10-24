#pragma once
// Included after the public types. This is the coupled equation/integration backend.
namespace analog {
inline double channel_current(double gate, double drain, double source, double beta, double threshold, bool p) {
    double polarity = p ? -1.0 : 1.0;
    gate *= polarity;
    drain *= polarity;
    source *= polarity;
    double sign = 1;
    if (drain < source) {
        std::swap(drain, source);
        sign = -1;
    }
    double overdrive = gate - source - threshold, ds = drain - source, current = 0;
    if (overdrive > 0)
        current = ds < overdrive ? beta * (overdrive * ds - ds * ds / 2) : beta * overdrive * overdrive / 2;
    return polarity * sign * current;
}

inline std::vector<double> linear_solve(std::vector<std::vector<double>> a, std::vector<double> b) {
    auto n = b.size();
    for (std::size_t i = 0; i < n; ++i) {
        double scale = 0;
        for (double v : a[i])
            scale = std::max(scale, std::abs(v));
        if (scale == 0)
            throw diagnostic("E_SINGULAR", "zero equation row; inspect floating wires and ideal constraints");
        for (double& v : a[i])
            v /= scale;
        b[i] /= scale;
    }
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t pivot = k;
        for (std::size_t i = k + 1; i < n; ++i)
            if (std::abs(a[i][k]) > std::abs(a[pivot][k]))
                pivot = i;
        if (std::abs(a[pivot][k]) < 1e-12)
            throw diagnostic("E_SINGULAR", "rank-deficient equation Jacobian");
        std::swap(a[k], a[pivot]);
        std::swap(b[k], b[pivot]);
        for (std::size_t i = k + 1; i < n; ++i) {
            if (a[i][k] == 0)
                continue; // Circuit matrices have many exact zeros; avoid arithmetic on an unchanged row.
            double factor = a[i][k] / a[k][k];
            for (std::size_t j = k + 1; j < n; ++j)
                a[i][j] -= factor * a[k][j];
            b[i] -= factor * b[k];
            a[i][k] = 0;
        }
    }
    std::vector<double> x(n);
    for (std::size_t ii = n; ii > 0; --ii) {
        auto i = ii - 1;
        double sum = b[i];
        for (std::size_t j = i + 1; j < n; ++j)
            sum -= a[i][j] * x[j];
        x[i] = sum / a[i][i];
    }
    return x;
}

inline void simulation::compile() {
    editable();
    for (const auto& component : components_)
        if (component.state != component_state::ready)
            throw diagnostic("E_ELABORATION", "IC " + component.name + " needs successful elaborate() before run");
    // Only wires participating in a model or a requested measurement need equations.
    // Inherited power ports can remain unused in passive or purely digital classes.
    std::vector<bool> used(parents_.size(), false);
    auto use = [&](std::size_t node) { used[root(node)] = true; };
    for (const auto& e : elements) {
        use(e.a);
        switch (e.kind) {
        case law::resistor:
        case law::inductor:
        case law::diode:
        case law::capacitor:
        case law::sim_voltage_source:
            use(e.b);
            break;
        case law::nmos:
        case law::pmos:
            use(e.b);
            use(e.gate);
            break;
        case law::transconductor:
        case law::sim_controlled_resistor:
            use(e.b);
            use(e.gate);
            use(e.reference);
            break;
        case law::sim_driver:
            use(e.vdd);
            use(e.vss);
            break;
        }
    }
    for (const auto& op : operations)
        if (op.kind == digital_law::sim_sense) {
            use(op.input);
            use(op.reference);
        }
    for (const auto& o : observations_)
        if (!o.digital) {
            use(o.a);
            use(o.b);
        }
    for (const auto& q : obligations_)
        if (!q.digital) {
            use(q.a);
            use(q.b);
        }
    index_.assign(parents_.size(), no_component);
    for (std::size_t i = 1; i < parents_.size(); ++i)
        if (root(i) == i && used[i]) {
            index_[i] = nodes_.size();
            nodes_.push_back(i);
        }
    unknowns_ = nodes_.size();
    for (auto& e : elements)
        if (e.kind == law::sim_voltage_source || e.kind == law::capacitor || e.kind == law::inductor)
            e.auxiliary = unknowns_++;
    // Capacitor initial voltages in a closed loop are dependent constraints.
    // Keep a spanning forest after the mandatory ideal voltage sources. The
    // remaining capacitor multipliers receive a zero-current gauge during
    // startup/hold only; their full C*dV/dt laws remain in every time step.
    std::vector<std::size_t> constraint_parent(parents_.size());
    for (std::size_t i = 0; i < constraint_parent.size(); ++i)
        constraint_parent[i] = i;
    auto find_constraint = [&](std::size_t node) {
        while (constraint_parent[node] != node) {
            constraint_parent[node] = constraint_parent[constraint_parent[node]];
            node = constraint_parent[node];
        }
        return node;
    };
    auto join_constraint = [&](std::size_t a, std::size_t b) {
        a = find_constraint(root(a));
        b = find_constraint(root(b));
        if (a == b)
            return false;
        constraint_parent[a] = b;
        return true;
    };
    for (const auto& e : elements)
        if (e.kind == law::sim_voltage_source)
            join_constraint(e.a, e.b);
    for (auto& e : elements)
        if (e.kind == law::capacitor)
            e.hold_basis = join_constraint(e.a, e.b);
    auto needs = [&](std::size_t n) {
        if (!digital.at(n).writer)
            throw diagnostic("E_UNDERCONSTRAINED", "a read bit signal has no assigning purpose or stimulus");
    };
    for (auto& op : operations) {
        if (op.kind == digital_law::sim_registered_output) {
            bool registered = std::any_of(clocked_.begin(), clocked_.end(),
                                          [&](const auto& process) { return process.identity == op.native_identity; });
            if (!registered || !op.read)
                throw diagnostic("E_UNDERCONSTRAINED", "native registered output needs a clocked module");
        }
        if (op.kind != digital_law::sim_sense && op.kind != digital_law::sim_registered_output)
            needs(op.a);
        if (op.kind == digital_law::sim_add)
            needs(op.b);
        if (op.kind == digital_law::sim_store) {
            needs(op.clock);
            digital[op.out].value = op.initial;
        }
    }
    for (const auto& process : clocked_)
        needs(process.clock);
    for (auto& e : elements)
        if (e.kind == law::sim_driver)
            needs(e.bit);
    for (auto& o : observations_)
        if (o.digital)
            needs(o.a);
    for (auto& q : obligations_)
        if (q.digital)
            needs(q.a);
    // Analog feedback has no topological order. Only pure digital combinational work does.
    std::vector<digital_operation> pending, ordered;
    std::vector<bool> available(digital.size());
    for (std::size_t i = 0; i < digital.size(); ++i)
        available[i] = digital[i].writer;
    for (auto& op : operations)
        if (op.kind != digital_law::sim_store && op.kind != digital_law::sim_sense &&
            op.kind != digital_law::sim_registered_output) {
            pending.push_back(op);
            available[op.out] = false;
        }
    while (!pending.empty()) {
        bool progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            if (available[it->a] && (it->kind != digital_law::sim_add || available[it->b])) {
                available[it->out] = true;
                ordered.push_back(*it);
                it = pending.erase(it);
                progress = true;
            } else
                ++it;
        }
        if (!progress)
            throw diagnostic("E_DIGITAL_CYCLE", "add explicit storage to break a digital cycle");
    }
    for (auto& op : operations)
        if (op.kind == digital_law::sim_store || op.kind == digital_law::sim_sense ||
            op.kind == digital_law::sim_registered_output)
            ordered.push_back(op);
    operations = std::move(ordered);
    obligation_samples_.resize(obligations_.size());
    assumption_pass_.assign(obligations_.size(), true);
    frozen_ = true;
}

inline std::pair<std::vector<double>, std::vector<double>> simulation::residual(const std::vector<double>& x,
                                                                                const std::vector<double>& old,
                                                                                double dt, double time, bool hold,
                                                                                bool initial, bool right) const {
    std::vector<double> f(nodes_.size(), 0), scales(nodes_.size(), 1e-3), extra;
    auto v = [&](std::size_t n) { return node_voltage(x, n); };
    auto previous = [&](std::size_t n) { return node_voltage(old, n); };
    auto stamp = [&](std::size_t a, std::size_t b, double current) {
        if (root(a) != 0)
            f[index_[root(a)]] += current;
        if (root(b) != 0)
            f[index_[root(b)]] -= current;
    };
    for (auto& e : elements)
        switch (e.kind) {
        case law::resistor:
            stamp(e.a, e.b, (v(e.a) - v(e.b)) / e.r);
            break;
        case law::sim_voltage_source: {
            stamp(e.a, e.b, x[e.auxiliary]);
            extra.push_back(v(e.a) - v(e.b) - source_value(e, time, right));
            scales.push_back(1);
            break;
        }
        case law::capacitor: {
            stamp(e.a, e.b, x[e.auxiliary]);
            if (hold) {
                extra.push_back(e.hold_basis ? v(e.a) - v(e.b) - (initial ? e.initial : previous(e.a) - previous(e.b))
                                             : x[e.auxiliary]);
                scales.push_back(e.hold_basis ? 1 : 1e-3);
            } else {
                // Integral voltage form avoids C/dt amplification of roundoff for large DC-blocking caps.
                const auto [leading, history] = derivative_weights(dt);
                const double memory = history == 0 ? 0 : history / leading *
                    (previous(e.a) - previous(e.b) - node_voltage(older_solution_, e.a) + node_voltage(older_solution_, e.b));
                extra.push_back(v(e.a) - v(e.b) - previous(e.a) + previous(e.b) - memory - dt / (leading * e.c) * x[e.auxiliary]);
                scales.push_back(std::max(1e-6, dt / (leading * e.c) * 1e-3));
            }
            break;
        }
        case law::inductor: {
            stamp(e.a, e.b, x[e.auxiliary]);
            if (hold) {
                extra.push_back(x[e.auxiliary] - (initial ? e.initial : old[e.auxiliary]));
                scales.push_back(1e-3);
            } else {
                const auto [leading, history] = derivative_weights(dt);
                const double memory = history == 0 ? 0 : history * (old[e.auxiliary] - older_solution_[e.auxiliary]);
                extra.push_back(v(e.a) - v(e.b) - e.l * (leading * (x[e.auxiliary] - old[e.auxiliary]) - memory) / dt);
                scales.push_back(1);
            }
            break;
        }
        case law::transconductor:
            stamp(e.a, e.b, -e.gm * (v(e.gate) - v(e.reference)));
            break;
        case law::diode: {
            double voltage = v(e.a) - v(e.b);
            stamp(e.a, e.b, std::max(0.0, voltage - e.threshold) / e.r + voltage / e.value);
            break;
        }
        case law::nmos:
        case law::pmos:
            stamp(e.a, e.b, channel_current(v(e.gate), v(e.a), v(e.b), e.beta, e.threshold, e.kind == law::pmos));
            break;
        case law::sim_driver: {
            auto bit = digital[e.bit].value;
            if (!bit)
                throw diagnostic("E_UNKNOWN_PHYSICAL", "unknown bit cannot select a physical driving rail");
            auto rail = *bit ? e.vdd : e.vss;
            stamp(e.a, rail, (v(e.a) - v(rail)) / e.r);
            break;
        }
        case law::sim_controlled_resistor: {
            double level = std::clamp((v(e.gate) - v(e.reference) - e.low) / (e.high - e.low), 0.0, 1.0);
            double conductance = (e.falling ? 1 - level : level) / e.r;
            stamp(e.a, e.buffered ? e.reference : e.b, conductance * (v(e.a) - v(e.b)));
            break;
        }
        }
    f.insert(f.end(), extra.begin(), extra.end());
    return {std::move(f), std::move(scales)};
}

// Exact piecewise derivatives avoid finite-difference steps crossing narrow switch/diode boundaries.
inline std::vector<std::vector<double>> simulation::jacobian(const std::vector<double>& x, double dt, bool hold) const {
    std::vector<std::vector<double>> j(x.size(), std::vector<double>(x.size()));
    stamp_jacobian(x, dt, hold, [&](std::size_t row, std::size_t column, double value) { j[row][column] += value; });
    return j;
}

inline void simulation::stamp_jacobian(const std::vector<double>& x, double dt, bool hold,
    const std::function<void(std::size_t, std::size_t, double)>& add) const {
    auto v = [&](std::size_t node) { return node_voltage(x, node); };
    auto row = [&](std::size_t equation, std::size_t node, double value) {
        if (root(node) != 0)
            add(equation, index_[root(node)], value);
    };
    auto derivative = [&](std::size_t a, std::size_t b, std::size_t control, double value) {
        if (root(a) != 0)
            row(index_[root(a)], control, value);
        if (root(b) != 0)
            row(index_[root(b)], control, -value);
    };
    auto pair = [&](std::size_t a, std::size_t b, std::size_t plus, std::size_t minus, double g) {
        derivative(a, b, plus, g);
        derivative(a, b, minus, -g);
    };
    auto branch_state = [&](const element& e) {
        if (root(e.a) != 0)
            add(index_[root(e.a)], e.auxiliary, 1);
        if (root(e.b) != 0)
            add(index_[root(e.b)], e.auxiliary, -1);
    };
    for (const auto& e : elements) {
        switch (e.kind) {
        case law::resistor:
            pair(e.a, e.b, e.a, e.b, 1 / e.r);
            break;
        case law::sim_voltage_source:
            branch_state(e);
            row(e.auxiliary, e.a, 1);
            row(e.auxiliary, e.b, -1);
            break;
        case law::capacitor:
            branch_state(e);
            if (hold) {
                if (e.hold_basis) {
                    row(e.auxiliary, e.a, 1);
                    row(e.auxiliary, e.b, -1);
                } else {
                    add(e.auxiliary, e.auxiliary, 1);
                }
            } else {
                add(e.auxiliary, e.auxiliary, -dt / (derivative_weights(dt).first * e.c));
                row(e.auxiliary, e.a, 1);
                row(e.auxiliary, e.b, -1);
            }
            break;
        case law::inductor:
            branch_state(e);
            if (hold)
                add(e.auxiliary, e.auxiliary, 1);
            else {
                row(e.auxiliary, e.a, 1);
                row(e.auxiliary, e.b, -1);
                add(e.auxiliary, e.auxiliary, -e.l * derivative_weights(dt).first / dt);
            }
            break;
        case law::transconductor:
            pair(e.a, e.b, e.gate, e.reference, -e.gm);
            break;
        case law::diode:
            pair(e.a, e.b, e.a, e.b, 1 / e.value + (v(e.a) - v(e.b) > e.threshold ? 1 / e.r : 0));
            break;
        case law::sim_driver: {
            auto rail = *digital[e.bit].value ? e.vdd : e.vss;
            pair(e.a, rail, e.a, rail, 1 / e.r);
            break;
        }
        case law::sim_controlled_resistor: {
            double raw = (v(e.gate) - v(e.reference) - e.low) / (e.high - e.low);
            double level = std::clamp(raw, 0.0, 1.0);
            double g = (e.falling ? 1 - level : level) / e.r;
            auto return_node = e.buffered ? e.reference : e.b;
            pair(e.a, return_node, e.a, e.b, g);
            if (raw > 0 && raw < 1) {
                double slope = (e.falling ? -1 : 1) / (e.r * (e.high - e.low));
                pair(e.a, return_node, e.gate, e.reference, slope * (v(e.a) - v(e.b)));
            }
            break;
        }
        case law::nmos:
        case law::pmos: {
            double polarity = e.kind == law::pmos ? -1 : 1;
            double gate = polarity * v(e.gate), drain = polarity * v(e.a), source = polarity * v(e.b);
            bool reverse = drain < source;
            if (reverse)
                std::swap(drain, source);
            double overdrive = gate - source - e.threshold, ds = drain - source;
            double gm = 0, gds = 0;
            if (overdrive > 0) {
                gm = e.beta * (ds < overdrive ? ds : overdrive);
                gds = ds < overdrive ? e.beta * (overdrive - ds) : 0;
            }
            derivative(e.a, e.b, e.gate, reverse ? -gm : gm);
            derivative(e.a, e.b, e.a, reverse ? gm + gds : gds);
            derivative(e.a, e.b, e.b, reverse ? -gds : -gm - gds);
            break;
        }
        }
    }
}

inline std::vector<double> simulation::solve(std::vector<double> x, const std::vector<double>& old, double dt,
                                             double time, bool hold, bool initial, bool right) {
    if (x.empty())
        return x;
    // Independent forcing is already known. Seed its exact voltages before Newton so a
    // clock-controlled conductance is linearized at the new clock, not at an obsolete off state.
    std::map<std::size_t, double> forcing{{0, 0}};
    for (std::size_t pass = 0; pass <= elements.size(); ++pass) {
        auto count = forcing.size();
        for (const auto& e : elements) {
            // Preserve the joint Newton update for ideal discontinuous source steps.
            // Continuous PWL forcing can safely be seeded at its new value before switch linearization.
            if (e.kind != law::sim_voltage_source || e.stepped)
                continue;
            auto a = root(e.a), b = root(e.b);
            double value = source_value(e, time, right);
            if (forcing.contains(b) && !forcing.contains(a))
                forcing[a] = forcing.at(b) + value;
            else if (forcing.contains(a) && !forcing.contains(b))
                forcing[b] = forcing.at(a) - value;
        }
        if (forcing.size() == count)
            break;
    }
    for (auto n : nodes_)
        if (forcing.contains(n))
            x[index_[n]] = forcing.at(n);
    auto norm = [](const auto& f, const auto& scales) {
        double n = 0;
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (!std::isfinite(f[i]))
                return std::numeric_limits<double>::infinity();
            n = std::max(n, std::abs(f[i]) / scales[i]);
        }
        return n;
    };
    for (int iteration = 0; iteration < 60; ++iteration) {
        ++newton_iterations;
        auto [f, scales] = residual(x, old, dt, time, hold, initial, right);
        double before = norm(f, scales);
        std::vector<std::vector<double>> jac;
        std::vector<matrix_entry> entries;
        if (linear_backend && analytic_jacobian) {
            stamp_jacobian(x, dt, hold, [&](std::size_t row, std::size_t column, double value) {
                if (value != 0)
                    entries.push_back({row, column, value / scales[row]});
            });
        } else if (analytic_jacobian) {
            jac = jacobian(x, dt, hold);
            for (std::size_t i = 0; i < jac.size(); ++i)
                for (auto& value : jac[i])
                    value /= scales[i];
        } else {
            if (!linear_backend)
                jac.assign(x.size(), std::vector<double>(x.size()));
            for (std::size_t j = 0; j < x.size(); ++j) {
                auto y = x;
                double h = 1e-6 * std::max(1.0, std::abs(x[j]));
                y[j] += h;
                auto [shifted, unused] = residual(y, old, dt, time, hold, initial, right);
                for (std::size_t i = 0; i < x.size(); ++i) {
                    const double value = (shifted[i] - f[i]) / h / scales[i];
                    if (linear_backend) {
                        if (value != 0)
                            entries.push_back({i, j, value});
                    } else {
                        jac[i][j] = value;
                    }
                }
            }
        }
        std::vector<double> rhs(f.size());
        for (std::size_t i = 0; i < f.size(); ++i)
            rhs[i] = -f[i] / scales[i];
        // Every backend must check rank even when the nonlinear residual is zero.
        auto delta = linear_backend ? linear_backend(x.size(), entries, rhs)
                                    : linear_solve(std::move(jac), std::move(rhs));
        if (delta.size() != x.size() || std::any_of(delta.begin(), delta.end(), [](double v) { return !std::isfinite(v); }))
            throw diagnostic("E_LINEAR_SOLVER", "linear backend returned an incomplete or nonfinite solution");
        if (before <= newton_residual_tolerance)
            return x;
        bool accepted = false;
        // Off channels can leave only a weak physical return and produce a huge Newton step.
        // Limit the trial voltage change before backtracking; this changes no device equation.
        double largest_voltage_step = 0, voltage_scale = 1;
        for (std::size_t j = 0; j < nodes_.size(); ++j) {
            largest_voltage_step = std::max(largest_voltage_step, std::abs(delta[j]));
            voltage_scale = std::max(voltage_scale, std::abs(x[j]));
        }
        double trial_limit = .25 * voltage_scale;
        double alpha = largest_voltage_step > trial_limit ? trial_limit / largest_voltage_step : 1;
        for (int attempt = 0; attempt < 24; ++attempt, alpha /= 2) {
            auto trial = x;
            for (std::size_t j = 0; j < x.size(); ++j)
                trial[j] += alpha * delta[j];
            auto [ff, ss] = residual(trial, old, dt, time, hold, initial, right);
            double after = norm(ff, ss);
            if (std::isfinite(after) && (after < before || after <= newton_residual_tolerance)) {
                x = std::move(trial);
                accepted = true;
                break;
            }
        }
        if (!accepted) {
            std::ostringstream detail;
            detail << "Newton line search failed at t=" << time << " s, dt=" << dt << " s; no infeasibility conclusion";
            throw diagnostic("E_NONCONVERGENCE", detail.str());
        }
    }
    std::ostringstream detail;
    detail << "Newton iteration limit at t=" << time << " s, dt=" << dt << " s";
    throw diagnostic("E_NONCONVERGENCE", detail.str());
}

inline void simulation::combinational() {
    for (auto& op : operations)
        if (op.kind == digital_law::sim_registered_output)
            digital[op.out].value = op.read();
    for (auto& op : operations) {
        if (op.kind == digital_law::sim_store || op.kind == digital_law::sim_sense ||
            op.kind == digital_law::sim_registered_output)
            continue;
        auto a = digital[op.a].value;
        if (!a || (op.kind == digital_law::sim_add && !digital[op.b].value)) {
            digital[op.out].value = std::nullopt;
            continue;
        }
        std::uint64_t value = op.kind == digital_law::sim_invert ? 1 - *a
                              : op.kind == digital_law::sim_add  ? *a + *digital[op.b].value
                                                                 : *a;
        unsigned width = digital[op.out].width;
        if (width < 64)
            value &= (std::uint64_t(1) << width) - 1;
        digital[op.out].value = value;
    }
}
inline void simulation::stimuli(double time, bool right) {
    for (auto& input : inputs) {
        if (input.kind == digital_stimulus::constant)
            digital[input.out].value = input.before;
        else if (input.kind == digital_stimulus::step)
            digital[input.out].value = (right ? time >= input.at : time > input.at) ? input.after : input.before;
        else {
            double q = time / (input.period / 2), near = std::round(q);
            const double tolerance = std::max(1e-10, 16 * std::numeric_limits<double>::epsilon() * std::abs(q));
            auto index = std::abs(q - near) < tolerance ? static_cast<long long>(near) - (right ? 0 : 1)
                                                    : static_cast<long long>(std::floor(q));
            digital[input.out].value = std::max(index, 0LL) % 2;
        }
    }
    combinational();
}
inline std::vector<double> simulation::settle(std::vector<double> x, const std::vector<double>& old, double dt,
                                              double time, bool hold, bool initial, bool right) {
    // Every hybrid iteration uses the same entry state in a hysteresis band.
    // A trial analog/digital iteration must not latch an intermediate excursion.
    // Rejected integration steps restore these bits with the existing rollback.
    std::vector<bit_value> retained_senses;
    for (const auto& op : operations)
        if (op.kind == digital_law::sim_sense && op.retain_in_band)
            retained_senses.push_back(digital[op.out].value);
    for (int i = 0; i < 24; ++i) {
        std::vector<bit_value> before;
        for (auto& b : digital)
            before.push_back(b.value);
        x = solve(std::move(x), old, dt, time, hold, initial, right);
        if (hold) {
            // A dependent constraint must still hold. Never silently discard
            // contradictory initial charge or choose a charge-sharing result
            // from declaration order when an ideal source jumps.
            for (const auto& e : elements)
                if (e.kind == law::capacitor && !e.hold_basis) {
                    double actual = node_voltage(x, e.a) - node_voltage(x, e.b);
                    double expected = initial ? e.initial : node_voltage(old, e.a) - node_voltage(old, e.b);
                    if (std::abs(actual - expected) > 1e-7 * std::max({1., std::abs(actual), std::abs(expected)}))
                        throw diagnostic(initial ? "E_INITIAL_CHARGE" : "E_CHARGE_DISCONTINUITY",
                            initial ? "inconsistent capacitor initial voltages in a constrained loop"
                                    : "ideal voltage jump conflicts with held capacitor charge; use a finite source edge");
                }
        }
        std::size_t retained_index = 0;
        for (auto& op : operations)
            if (op.kind == digital_law::sim_sense) {
                double v = node_voltage(x, op.input) - node_voltage(x, op.reference);
                const bit_value in_band = op.retain_in_band ? retained_senses[retained_index++] : std::nullopt;
                digital[op.out].value = v <= op.low ? bit_value(0) : v >= op.high ? bit_value(1) : in_band;
            }
        combinational();
        bool changed = false;
        for (std::size_t j = 0; j < digital.size(); ++j)
            changed |= before[j] != digital[j].value;
        if (!changed)
            return x;
    }
    throw diagnostic("E_HYBRID_CYCLE", "analog/bit boundary did not settle");
}
inline void simulation::record(double time) {
    sample row{time, {}, {}};
    for (auto& o : observations_) {
        if (o.digital)
            row.values.push_back(digital[o.a].value ? std::optional<double>(double(*digital[o.a].value))
                                                    : std::nullopt);
        else
            row.values.push_back(node_voltage(solution_, o.a) - node_voltage(solution_, o.b));
        row.exact_bits.push_back(o.digital ? digital[o.a].value : std::nullopt);
    }
    trace.push_back(std::move(row));
    for (std::size_t i = 0; i < obligations_.size(); ++i) {
        auto& q = obligations_[i];
        obligation_samples_[i] =
            q.digital ? digital[q.a].value ? std::optional<double>(double(*digital[q.a].value)) : std::nullopt
                      : std::optional<double>(node_voltage(solution_, q.a) - node_voltage(solution_, q.b));
        const auto value = obligation_samples_[i];
        if (q.assumption)
            assumption_pass_[i] = assumption_pass_[i] && value && *value >= q.low - 1e-9 && *value <= q.high + 1e-9;
    }
    accepted_time_ = time;
}

inline void validate_transient_span(double start, duration stop, duration step, duration minimum_step) {
    positive(stop.value, "stop time");
    positive(step.value, "step time");
    finite(minimum_step.value, "minimum step time");
    if (minimum_step.value < 0)
        throw diagnostic("E_VALUE", "minimum step time must be nonnegative");
    const double span = stop.value - start;
    if (span <= 0 || step.value > span || span / step.value > 100000.000000001)
        throw diagnostic("E_VALUE", "invalid step or more than 100000 regular steps");
    if (!std::isfinite(stop.value / step.value) || stop.value / step.value > 9007199254740991. ||
        (start > 0 && start + step.value <= start))
        throw diagnostic("E_VALUE", "absolute time grid exceeds representable resolution");
}

inline void simulation::run(duration stop, duration step) {
    if (transient_started_)
        throw diagnostic("E_ELABORATION", "a transient already started; use advance_to to preserve its state");
    validate_transient_span(0, stop, step, minimum_step);
    try {
        integrate(stop, step, false);
    } catch (...) {
        transient_failed_ = transient_started_;
        status_ = "unverified";
        throw;
    }
}

inline void simulation::advance_to(duration stop, duration step) {
    if (!transient_started_ || transient_failed_)
        throw diagnostic("E_TRANSIENT_STATE", "continuation needs a successfully initialized, healthy transient");
    validate_transient_span(accepted_time_, stop, step, minimum_step);
    try {
        integrate(stop, step, true);
    } catch (...) {
        transient_failed_ = true;
        status_ = "unverified";
        throw;
    }
}

inline void simulation::integrate(duration stop, duration step, bool continuation) {
    const double start = continuation ? accepted_time_ : 0;
    status_ = "unverified";
    for (auto& obligation : obligations_)
        obligation.status = "unverified";
    for (auto& obligation : trace_obligations_)
        obligation.status = "unverified";
    if (!continuation) {
        compile();
        transient_started_ = true;
        // Reset all native next-state values before committing any native register.
        for (auto& process : clocked_)
            process.work(true);
        for (auto& process : clocked_)
            process.commit();
        // Seed independent sources/initial charges before nonlinear device iteration.
        solution_.assign(unknowns_, 0);
        std::map<std::size_t, double> known{{0, 0}};
        for (std::size_t pass = 0; pass <= elements.size(); ++pass)
            for (auto& e : elements) {
                if (e.kind != law::sim_voltage_source && e.kind != law::capacitor)
                    continue;
                double value = e.kind == law::capacitor ? e.initial : source_value(e, 0);
                auto a = root(e.a), b = root(e.b);
                if (known.contains(b) && !known.contains(a))
                    known[a] = known[b] + value;
                else if (known.contains(a) && !known.contains(b))
                    known[b] = known[a] - value;
            }
        // Start resistively supplied nodes near their neighbors' known voltages.
        // These are Newton guesses only: no source/charge initialization is overwritten.
        auto guess = known;
        for (std::size_t pass = 0; pass <= elements.size(); ++pass)
            for (const auto& e : elements) {
                if (e.kind != law::resistor)
                    continue;
                auto a = root(e.a), b = root(e.b);
                if (guess.contains(a) && !guess.contains(b))
                    guess[b] = guess[a];
                else if (guess.contains(b) && !guess.contains(a))
                    guess[a] = guess[b];
            }
        for (auto n : nodes_)
            if (guess.contains(n))
                solution_[index_[n]] = guess[n];
        stimuli(0, true);
        auto initial = solution_;
        solution_ = settle(solution_, initial, 0, 0, true, true);
        previous_clocks_.assign(digital.size(), std::nullopt);
        for (auto& op : operations)
            if (op.kind == digital_law::sim_store)
                previous_clocks_[op.clock] = digital[op.clock].value;
        for (const auto& process : clocked_) {
            if (!digital[process.clock].value)
                throw diagnostic("E_UNKNOWN_CPPHDL", "native module requires a known clock level");
            previous_clocks_[process.clock] = digital[process.clock].value;
        }
        record(0);
    }
    auto& previous_clocks = previous_clocks_;
    std::vector<std::pair<double, int>> schedule{{start, 2}, {stop.value, 2}};
    for (std::size_t i = std::size_t(std::floor(start / step.value)) + 1;
         i <= std::size_t(std::floor(stop.value / step.value)); ++i) {
        const double time = i * step.value;
        if (time > start && time <= stop.value)
            schedule.emplace_back(time, 0);
    }
    for (auto& e : elements) {
        if (e.stepped && e.at > start && e.at <= stop.value)
            schedule.emplace_back(e.at, 1);
        for (const auto& point : e.waveform)
            if (point.first > start && point.first <= stop.value)
                schedule.emplace_back(point.first, 0);
    }
    for (auto& input : inputs) {
        if (input.kind == digital_stimulus::step && input.at > start && input.at <= stop.value)
            schedule.emplace_back(input.at, 1);
        if (input.kind == digital_stimulus::clock) {
            double count = stop.value / (input.period / 2);
            const double first = std::floor(start / (input.period / 2)) + 1;
            if (!std::isfinite(count) || count > 9007199254740991. || count - first + 1 > 100000.000000001)
                throw diagnostic("E_VALUE", "too many clock events");
            for (std::size_t i = std::size_t(first); i <= std::size_t(std::floor(count)); ++i) {
                const double time = i * input.period / 2;
                if (time > start && time <= stop.value)
                    schedule.emplace_back(time, 1);
            }
        }
    }
    std::sort(schedule.begin(), schedule.end());
    std::vector<std::pair<double, int>> times;
    for (auto item : schedule) {
        const double tolerance = std::max(stop.value, 1e-15) * 8 * std::numeric_limits<double>::epsilon();
        if (times.empty() || std::abs(item.first - times.back().first) > tolerance)
            times.push_back(item);
        else if (item.second > times.back().second)
            times.back() = item;
    }
    if (times.size() > 200000)
        throw diagnostic("E_VALUE", "more than 200000 distinct transient events in one integration window");
    std::vector<double> discontinuities;
    for (const auto& e : elements)
        for (const auto& point : e.waveform)
            discontinuities.push_back(point.first);
    // Keep event identity even when the segment-end marker wins timestamp
    // merging. The next advance still needs a first-order restart.
    for (const auto& item : schedule)
        if (item.second == 1)
            discontinuities.push_back(item.first);
    std::sort(discontinuities.begin(), discontinuities.end());
    double previous_time = start;
    for (std::size_t i = 1; i < times.size(); ++i) {
        double time = times[i].first;
        auto previous = solution_;
        std::vector<bit_value> previous_bits;
        if (minimum_step.value > 0 || method == integration_method::bdf2)
            for (const auto& bit : digital)
                previous_bits.push_back(bit.value);
        stimuli(time, false);
        try {
            solution_ = settle(solution_, previous, time - previous_time, time, false, false, false);
        } catch (const diagnostic& error) {
            // A numerically inaccurate Newton direction is a rejected step,
            // not an accepted circuit state. Opt-in subdivision may retry it
            // at a smaller dt; structural rank/input failures remain fatal.
            if ((error.code != "E_NONCONVERGENCE" && error.code != "E_LINEAR_ACCURACY") || minimum_step.value <= 0 ||
                time - previous_time <= 2 * minimum_step.value || times.size() >= 200000)
                throw;
            for (std::size_t bit = 0; bit < previous_bits.size(); ++bit)
                digital[bit].value = previous_bits[bit];
            times.insert(times.begin() + i, {(previous_time + time) / 2, 0});
            --i;
            continue;
        }
        const auto commits_before = register_commits;
        auto held = solution_;
        stimuli(time, true);
        solution_ = settle(solution_, held, 0, time, true);
        bool finished = false;
        for (int delta = 0; delta < 24; ++delta) {
            std::vector<std::pair<std::size_t, bit_value>> updates;
            std::vector<clocked_process*> active;
            for (auto& op : operations)
                if (op.kind == digital_law::sim_store) {
                    auto old = previous_clocks[op.clock], now = digital[op.clock].value;
                    if (old == bit_value(0) && now == bit_value(1))
                        updates.emplace_back(op.out, digital[op.a].value);
                    else if ((!old && now == bit_value(1)) || (old == bit_value(0) && !now))
                        updates.emplace_back(op.out, std::nullopt);
                }
            for (auto& process : clocked_) {
                auto old = previous_clocks[process.clock], now = digital[process.clock].value;
                if (!old || !now)
                    throw diagnostic("E_UNKNOWN_CPPHDL", "ambiguous clock cannot advance a two-state native module");
                if (old == bit_value(0) && now == bit_value(1))
                    active.push_back(&process);
            }
            for (auto& op : operations)
                if (op.kind == digital_law::sim_store)
                    previous_clocks[op.clock] = digital[op.clock].value;
            for (const auto& process : clocked_)
                previous_clocks[process.clock] = digital[process.clock].value;
            if (updates.empty() && active.empty()) {
                finished = true;
                break;
            }
            // Native _work sees the same old-state snapshot as analog-domain stores.
            for (auto* process : active)
                process->work(false);
            ++register_commits;
            for (auto& [out, value] : updates)
                digital[out].value = value; // Atomic old-state/next-state commit.
            for (auto* process : active)
                process->commit();
            combinational();
            solution_ = settle(solution_, held, 0, time, true);
        }
        if (!finished)
            throw diagnostic("E_EVENT_CYCLE", "generated clock delta cycle");
        older_solution_ = std::move(previous);
        previous_dt_ = time - previous_time;
        const double knot_tolerance = std::max(stop.value, 1e-15) * 8 * std::numeric_limits<double>::epsilon();
        const auto knot = std::lower_bound(discontinuities.begin(), discontinuities.end(), time - knot_tolerance);
        bool digital_changed = false;
        if (method == integration_method::bdf2)
            for (std::size_t bit = 0; bit < previous_bits.size(); ++bit)
                digital_changed = digital_changed || previous_bits[bit] != digital[bit].value;
        // Analog sensing can change a physical driver without a register
        // commit or scheduled source event. Its old derivative is invalid.
        history_ready_ = !digital_changed && times[i].second != 1 && commits_before == register_commits &&
            (knot == discontinuities.end() || *knot > time + knot_tolerance);
        record(time);
        previous_time = time;
    }
    bool valid = true, failed = false, unknown = false;
    for (std::size_t i = 0; i < obligations_.size(); ++i)
        if (obligations_[i].assumption) {
            auto& q = obligations_[i];
            const bool pass = assumption_pass_[i];
            q.status = pass ? "sampled_pass" : "violated";
            valid &= pass;
        }
    for (std::size_t i = 0; i < obligations_.size(); ++i)
        if (!obligations_[i].assumption) {
            auto& q = obligations_[i];
            auto value = obligation_samples_[i];
            if (!valid)
                q.status = "out_of_contract";
            else if (!value) {
                q.status = "unverified";
                unknown = true;
            } else {
                bool pass = q.digital ? digital[q.a].value && *digital[q.a].value == q.expected
                                      : *value >= q.low - 1e-8 && *value <= q.high + 1e-8;
                q.status = pass ? "sampled_pass" : "sampled_fail";
                failed |= !pass;
            }
        }
    for (auto& q : trace_obligations_) {
        if (!valid) {
            q.status = "out_of_contract";
            continue;
        }
        auto pass = q.predicate(trace);
        q.status = !pass ? "unverified" : *pass ? "sampled_pass" : "sampled_fail";
        unknown |= !pass;
        failed |= pass && !*pass;
    }
    status_ = !valid ? "out_of_contract" : failed ? "failed" : unknown ? "unverified" : "sampled_pass";
}

inline const char* law_name(law kind) {
    switch (kind) {
    case law::resistor:
        return "resistor";
    case law::capacitor:
        return "capacitor";
    case law::sim_voltage_source:
        return "sim_voltage_source";
    case law::nmos:
        return "nmos";
    case law::pmos:
        return "pmos";
    case law::sim_driver:
        return "sim_driver";
    case law::sim_controlled_resistor:
        return "sim_controlled_resistor";
    case law::inductor:
        return "inductor";
    case law::transconductor:
        return "transconductor";
    case law::diode:
        return "diode";
    }
    return "unsupported";
}
inline std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n')
            out += "\\n";
        else if (c < 32) {
            std::ostringstream hex;
            hex << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
            out += hex.str();
        } else
            out += char(c);
    }
    return out + '"';
}
inline void simulation::write(const std::filesystem::path& destination) const {
    std::filesystem::create_directories(destination);
    std::ofstream csv(destination / "trace.csv"), report(destination / "report.json"),
        equations(destination / "equations.txt");
    if (!csv || !report || !equations)
        throw diagnostic("E_IO", "cannot create result files");
    csv << std::setprecision(17) << "time_s";
    for (auto& o : observations_)
        csv << ',' << json_string(o.name);
    csv << '\n';
    for (auto& row : trace) {
        csv << row.time;
        for (std::size_t i = 0; i < row.values.size(); ++i) {
            csv << ',';
            if (observations_[i].digital) {
                if (row.exact_bits[i])
                    csv << *row.exact_bits[i];
            } else if (row.values[i])
                csv << *row.values[i];
        }
        csv << '\n';
    }
    report << std::setprecision(17) << "{\n  \"language\": \"Analog C++ 0.3\",\n  \"status\": " << json_string(status_)
           << ",\n  \"samples\": " << trace.size() << ",\n  \"newton_iterations\": " << newton_iterations
           << ",\n  \"register_commits\": " << register_commits << ",\n  \"final\": {";
    for (std::size_t i = 0; i < observations_.size(); ++i) {
        if (i)
            report << ',';
        report << "\n    " << json_string(observations_[i].name) << ": ";
        if (observations_[i].digital) {
            auto v = digital[observations_[i].a].value;
            if (v)
                report << *v;
            else
                report << "null";
        } else if (!trace.empty())
            report << *trace.back().values[i];
        else
            report << "null";
    }
    report << "\n  },\n  \"obligations\": [";
    for (std::size_t i = 0; i < obligations_.size(); ++i) {
        if (i)
            report << ',';
        auto& q = obligations_[i];
        report << "\n    {\"name\": " << json_string(q.name)
               << ", \"assumption\": " << (q.assumption ? "true" : "false")
               << ", \"status\": " << json_string(q.status);
        if (q.digital) {
            report << ", \"expected\": " << q.expected << ", \"actual\": ";
            if (digital[q.a].value)
                report << *digital[q.a].value;
            else
                report << "null";
        } else
            report << ", \"lower_V\": " << q.low << ", \"upper_V\": " << q.high
                   << ", \"final_V\": " << node_voltage(solution_, q.a) - node_voltage(solution_, q.b);
        report << '}';
    }
    for (std::size_t i = 0; i < trace_obligations_.size(); ++i) {
        if (i || !obligations_.empty())
            report << ',';
        const auto& q = trace_obligations_[i];
        report << "\n    {\"name\": " << json_string(q.name)
               << ", \"kind\": \"trace\", \"assumption\": false, \"status\": " << json_string(q.status) << '}';
    }
    report << "\n  ],\n  \"components\": [";
    for (std::size_t i = 0; i < components_.size(); ++i) {
        if (i)
            report << ',';
        const auto& component = components_[i];
        report << "\n    {\"id\": " << component.id << ", \"name\": " << json_string(component.name)
               << ", \"state\": \"elaborated\"}";
    }
    report << "\n  ],\n  \"compiler\": " << json_string(__VERSION__)
           << ",\n  \"evidence\": \"sampled selected-model trajectory; not a proof of every "
              "purpose or operating condition\",\n"
           << "  \"limitations\": [\"fixed-step backward Euler; no error control\", \"sampled "
              "analog thresholds; no metastability or setup/hold\", \"square-law MOS omits leakage, body and intrinsic "
              "charge\"]\n}\n";
    equations << std::setprecision(17)
              << "Analog C++ 0.3: purpose declarations elaborated to shared terminal relations.\n"
              << "For each nonreference node: sum of outgoing terminal currents = 0.\n"
              << "Node 0 is the explicit reference; node aliases share a canonical number.\n";
    for (const auto& component : components_)
        equations << "IC " << component.id << ": " << component.name << '\n';
    for (std::size_t i = 0; i < elements.size(); ++i) {
        auto& e = elements[i];
        equations << "device " << i << ": " << (e.buffered ? "sim_buffered_track" : law_name(e.kind)) << " a=n"
                  << root(e.a) << " b=n" << root(e.b) << " gate=n" << root(e.gate) << " Vdd=n" << root(e.vdd)
                  << " Vss=n" << root(e.vss) << " bit=d" << e.bit << " R=" << e.r << " C=" << e.c << " beta=" << e.beta
                  << " threshold=" << e.threshold << " initial=" << e.initial << " source=" << e.value
                  << " step_at=" << e.at << " next=" << e.next << " stepped=" << e.stepped;
        if (e.kind == law::sim_controlled_resistor)
            equations << " control_reference=n" << root(e.reference) << " low=" << e.low << " high=" << e.high
                      << " response=" << (e.falling ? "falling" : "rising");
        if (e.kind == law::inductor)
            equations << " L=" << e.l << " initial_current=" << e.initial;
        if (e.kind == law::transconductor)
            equations << " gm=" << e.gm << " control_plus=n" << root(e.gate) << " control_minus=n" << root(e.reference);
        for (const auto& point : e.waveform)
            equations << " pwl=(" << point.first << ',' << point.second << ')';
        if (e.component != no_component)
            equations << " IC=" << e.component;
        equations << "; origin " << e.file << ':' << e.line << '\n';
    }
    // Expose the actual coupling: every device's signed current enters shared-node KCL.
    std::map<std::size_t, std::vector<std::string>> incidence;
    auto branch = [&](std::size_t a, std::size_t b, const std::string& name) {
        if (root(a) != 0)
            incidence[root(a)].push_back("+" + name);
        if (root(b) != 0)
            incidence[root(b)].push_back("-" + name);
    };
    for (std::size_t i = 0; i < elements.size(); ++i) {
        auto& e = elements[i];
        auto name = "I" + std::to_string(i);
        if (e.kind == law::sim_driver) {
            branch(e.a, e.vdd, name + "_vdd");
            branch(e.a, e.vss, name + "_vss");
        } else
            branch(e.a, e.buffered ? e.reference : e.b, name);
    }
    for (auto n : nodes_) {
        equations << "KCL n" << n << ": ";
        for (auto& term : incidence[n])
            equations << term << ' ';
        equations << "= 0\n";
    }
    equations << "R: I(a->b)=(Va-Vb)/R\nC: I(a->b)=C*d(Va-Vb)/dt\n"
              << "sim_voltage_source: Va-Vb=waveform(t), I(a->b) is an auxiliary unknown\n"
              << "L: Va-Vb=L*dI(a->b)/dt; initial state is branch current\n"
              << "transconductor: I(a->b)=-gm*(Vcontrol_plus-Vcontrol_minus)\n"
              << "diode: I(a->b)=max(Va-Vb-drop,0)/Ron+(Va-Vb)/Rleak\n"
              << "sim_buffered_track: I(a->reference)=G(clock)*(Va-Vinput); input current=0\n"
              << "MOS: terminal-current square-law relation, with symmetric drain/source handling\n"
              << "sim_driver: I_vdd=bit*(Va-Vdd)/R; I_vss=(1-bit)*(Va-Vss)/R\n"
              << "sim_controlled_resistor: f=clip((Vcontrol-Vreference-low)/(high-low),0,1); "
                 "G=(falling ? 1-f : f)/R; I(a->b)=G*(Va-Vb)\n"
              << "clocked bits: compute every active next state from one old-state snapshot, then "
                 "commit together\n";
    for (auto& op : operations) {
        equations << "digital purpose: d" << op.out << " = ";
        switch (op.kind) {
        case digital_law::sim_add:
            equations << "sim_add(d" << op.a << ",d" << op.b << ") modulo 2^" << digital[op.out].width;
            break;
        case digital_law::copy:
            equations << "copy(d" << op.a << ")";
            break;
        case digital_law::sim_invert:
            equations << "sim_invert(d" << op.a << ")";
            break;
        case digital_law::sim_store:
            equations << "sim_store(d" << op.a << ", rising d" << op.clock << ", initial=" << op.initial << ")";
            break;
        case digital_law::sim_sense:
            equations << (op.retain_in_band ? "sim_schmitt_sense(V(n" : "sim_sense(V(n")
                      << root(op.input) << ")-V(n" << root(op.reference) << "),low=" << op.low
                      << ",high=" << op.high << ")";
            break;
        case digital_law::sim_registered_output:
            equations << "sim_registered_output(two-state value, width=" << digital[op.out].width << ")";
            break;
        }
        equations << '\n';
    }
    for (const auto& process : clocked_)
        equations << "sim_clock_cpphdl: reset once; _work(false) on rising d" << process.clock
                  << "; all _work calls precede all _strobe calls\n";
    for (auto& o : observations_)
        if (!o.digital)
            equations << "observe " << o.name << " = V(n" << root(o.a) << ")-V(n" << root(o.b) << ")\n";
}
} // namespace analog
