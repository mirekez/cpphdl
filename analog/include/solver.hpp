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
        case law::capacitor:
        case law::sim_voltage_source:
            use(e.b);
            break;
        case law::nmos:
        case law::pmos:
            use(e.b);
            use(e.gate);
            break;
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
        if (e.kind == law::sim_voltage_source || e.kind == law::capacitor)
            e.auxiliary = unknowns_++;
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
            bool after = e.stepped && (right ? time >= e.at : time > e.at);
            stamp(e.a, e.b, x[e.auxiliary]);
            extra.push_back(v(e.a) - v(e.b) - (after ? e.next : e.value));
            scales.push_back(1);
            break;
        }
        case law::capacitor: {
            stamp(e.a, e.b, x[e.auxiliary]);
            if (hold) {
                extra.push_back(v(e.a) - v(e.b) - (initial ? e.initial : previous(e.a) - previous(e.b)));
                scales.push_back(1);
            } else {
                extra.push_back(x[e.auxiliary] - e.c * (v(e.a) - v(e.b) - previous(e.a) + previous(e.b)) / dt);
                scales.push_back(1e-3);
            }
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
            stamp(e.a, e.b, conductance * (v(e.a) - v(e.b)));
            break;
        }
        }
    f.insert(f.end(), extra.begin(), extra.end());
    return {std::move(f), std::move(scales)};
}

inline std::vector<double> simulation::solve(std::vector<double> x, const std::vector<double>& old, double dt,
                                             double time, bool hold, bool initial, bool right) {
    if (x.empty())
        return x;
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
        std::vector<std::vector<double>> jac(x.size(), std::vector<double>(x.size()));
        for (std::size_t j = 0; j < x.size(); ++j) {
            auto y = x;
            double h = 1e-6 * std::max(1.0, std::abs(x[j]));
            y[j] += h;
            auto [shifted, unused] = residual(y, old, dt, time, hold, initial, right);
            for (std::size_t i = 0; i < x.size(); ++i)
                jac[i][j] = (shifted[i] - f[i]) / h / scales[i];
        }
        std::vector<double> rhs(f.size());
        for (std::size_t i = 0; i < f.size(); ++i)
            rhs[i] = -f[i] / scales[i];
        auto delta = linear_solve(std::move(jac),
                                  std::move(rhs)); // Rank check even when all residuals are zero.
        if (before <= 1e-8)
            return x;
        bool accepted = false;
        double alpha = 1;
        for (int attempt = 0; attempt < 24; ++attempt, alpha /= 2) {
            auto trial = x;
            for (std::size_t j = 0; j < x.size(); ++j)
                trial[j] += alpha * delta[j];
            auto [ff, ss] = residual(trial, old, dt, time, hold, initial, right);
            double after = norm(ff, ss);
            if (std::isfinite(after) && (after < before || after <= 1e-8)) {
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
    throw diagnostic("E_NONCONVERGENCE", "Newton iteration limit");
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
            auto index = std::abs(q - near) < 1e-10 ? static_cast<long long>(near) - (right ? 0 : 1)
                                                    : static_cast<long long>(std::floor(q));
            digital[input.out].value = std::max(index, 0LL) % 2;
        }
    }
    combinational();
}
inline std::vector<double> simulation::settle(std::vector<double> x, const std::vector<double>& old, double dt,
                                              double time, bool hold, bool initial, bool right) {
    for (int i = 0; i < 24; ++i) {
        std::vector<bit_value> before;
        for (auto& b : digital)
            before.push_back(b.value);
        x = solve(std::move(x), old, dt, time, hold, initial, right);
        for (auto& op : operations)
            if (op.kind == digital_law::sim_sense) {
                double v = node_voltage(x, op.input) - node_voltage(x, op.reference);
                digital[op.out].value = v <= op.low ? bit_value(0) : v >= op.high ? bit_value(1) : std::nullopt;
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
        obligation_samples_[i].push_back(
            q.digital ? digital[q.a].value ? std::optional<double>(double(*digital[q.a].value)) : std::nullopt
                      : std::optional<double>(node_voltage(solution_, q.a) - node_voltage(solution_, q.b)));
    }
}

inline void simulation::run(duration stop, duration step) {
    positive(stop.value, "stop time");
    positive(step.value, "step time");
    if (step.value > stop.value || stop.value / step.value > 100000)
        throw diagnostic("E_VALUE", "invalid step or more than 100000 regular steps");
    compile();
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
            double value = e.kind == law::capacitor ? e.initial : e.stepped && e.at == 0 ? e.next : e.value;
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
    std::vector<bit_value> previous_clocks(digital.size());
    for (auto& op : operations)
        if (op.kind == digital_law::sim_store)
            previous_clocks[op.clock] = digital[op.clock].value;
    for (const auto& process : clocked_) {
        if (!digital[process.clock].value)
            throw diagnostic("E_UNKNOWN_CPPHDL", "native module requires a known clock level");
        previous_clocks[process.clock] = digital[process.clock].value;
    }
    record(0);
    std::vector<std::pair<double, int>> schedule{{0, 2}, {stop.value, 2}};
    for (std::size_t i = 1; i <= std::size_t(std::floor(stop.value / step.value)); ++i)
        schedule.emplace_back(i * step.value, 0);
    for (auto& e : elements)
        if (e.stepped && e.at <= stop.value)
            schedule.emplace_back(e.at, 1);
    for (auto& input : inputs) {
        if (input.kind == digital_stimulus::step && input.at <= stop.value)
            schedule.emplace_back(input.at, 1);
        if (input.kind == digital_stimulus::clock) {
            double count = stop.value / (input.period / 2);
            if (count > 100000)
                throw diagnostic("E_VALUE", "too many clock events");
            for (std::size_t i = 1; i <= std::size_t(count); ++i)
                schedule.emplace_back(i * input.period / 2, 1);
        }
    }
    std::sort(schedule.begin(), schedule.end());
    std::vector<std::pair<double, int>> times;
    for (auto item : schedule) {
        if (times.empty() || std::abs(item.first - times.back().first) > std::max(stop.value, 1e-15) * 1e-12)
            times.push_back(item);
        else if (item.second > times.back().second)
            times.back() = item;
    }
    double previous_time = 0;
    for (std::size_t i = 1; i < times.size(); ++i) {
        double time = times[i].first;
        auto previous = solution_;
        stimuli(time, false);
        solution_ = settle(solution_, previous, time - previous_time, time, false, false, false);
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
        record(time);
        previous_time = time;
    }
    bool valid = true, failed = false, unknown = false;
    for (std::size_t i = 0; i < obligations_.size(); ++i)
        if (obligations_[i].assumption) {
            auto& q = obligations_[i];
            bool pass = true;
            for (auto v : obligation_samples_[i])
                pass &= v && *v >= q.low - 1e-9 && *v <= q.high + 1e-9;
            q.status = pass ? "sampled_pass" : "violated";
            valid &= pass;
        }
    for (std::size_t i = 0; i < obligations_.size(); ++i)
        if (!obligations_[i].assumption) {
            auto& q = obligations_[i];
            auto value = obligation_samples_[i].back();
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
        equations << "device " << i << ": " << law_name(e.kind) << " a=n" << root(e.a) << " b=n" << root(e.b)
                  << " gate=n" << root(e.gate) << " Vdd=n" << root(e.vdd) << " Vss=n" << root(e.vss) << " bit=d"
                  << e.bit << " R=" << e.r << " C=" << e.c << " beta=" << e.beta << " threshold=" << e.threshold
                  << " initial=" << e.initial << " source=" << e.value << " step_at=" << e.at << " next=" << e.next
                  << " stepped=" << e.stepped;
        if (e.kind == law::sim_controlled_resistor)
            equations << " control_reference=n" << root(e.reference) << " low=" << e.low << " high=" << e.high
                      << " response=" << (e.falling ? "falling" : "rising");
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
            branch(e.a, e.b, name);
    }
    for (auto n : nodes_) {
        equations << "KCL n" << n << ": ";
        for (auto& term : incidence[n])
            equations << term << ' ';
        equations << "= 0\n";
    }
    equations << "R: I(a->b)=(Va-Vb)/R\nC: I(a->b)=C*d(Va-Vb)/dt\n"
              << "sim_voltage_source: Va-Vb=waveform(t), I(a->b) is an auxiliary unknown\n"
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
            equations << "sim_sense(V(n" << root(op.input) << ")-V(n" << root(op.reference) << "),low=" << op.low
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
