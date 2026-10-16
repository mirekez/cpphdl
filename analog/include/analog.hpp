#pragma once
#ifdef CPPHDL_STATIC
#error "Analog ICs require per-instance ports; CPPHDL_STATIC is unsupported"
#endif
#include "units.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <cpphdl_port_traits.h>

namespace analog {
class simulation;
class IC;
class electrical;
template <class T> class port;
template <unsigned W> class bits;
using bit_value = std::optional<std::uint64_t>;
inline constexpr std::size_t no_component = std::numeric_limits<std::size_t>::max();

struct diagnostic : std::runtime_error {
    std::string code;
    diagnostic(std::string c, const std::string& message)
        : std::runtime_error(c + ": " + message), code(std::move(c)) {}
};

// A wire is an identity in the shared network. Copies reference that same wire.
class electrical {
    simulation* owner_;
    std::size_t id_;

public:
    using cpphdl_connection_tag = void;
    template <class T> using cpphdl_port_type = analog::port<T>;
    electrical();
    electrical(const electrical&) = default;
    // '=' declares the purpose/connection. It retains this wire's identity.
    electrical& operator=(const electrical& other);
    simulation& owner() const {
        return *owner_;
    }
    std::size_t id() const {
        return id_;
    }
};

enum class law { resistor, capacitor, sim_voltage_source, nmos, pmos, sim_driver, sim_controlled_resistor };
enum class control_response { rising, falling };
struct element {
    law kind;
    std::size_t a = 0, b = 0, gate = 0, reference = 0, vdd = 0, vss = 0, bit = 0;
    double r = 1, c = 0, beta = .001, threshold = .35, initial = 0;
    double value = 0, next = 0, at = 0, low = .35, high = .85;
    bool stepped = false;
    bool falling = false;
    std::size_t auxiliary = 0;
    std::string file{};
    unsigned line = 0;
    std::size_t component = no_component;
};

enum class component_state { declared, assigning, ready, failed };
struct component_info {
    std::size_t id;
    std::string name;
    component_state state = component_state::declared;
};

struct bit_state {
    unsigned width;
    bit_value value;
    bool writer = false;
};
enum class digital_law { sim_add, copy, sim_invert, sim_store, sim_sense, sim_registered_output };
struct digital_operation {
    digital_law kind;
    std::size_t out = 0, a = 0, b = 0, clock = 0, input = 0, reference = 0;
    std::uint64_t initial = 0;
    double low = 0, high = 0;
    std::function<bit_value()> read{};
    const void* native_identity = nullptr;
};
struct clocked_process {
    std::size_t clock;
    const void* identity;
    std::function<void(bool)> work;
    std::function<void()> commit;
};
struct digital_stimulus {
    enum mode { constant, clock, step } kind;
    std::size_t out;
    double period = 0, at = 0;
    std::uint64_t before = 0, after = 0;
};
struct observation {
    std::string name;
    bool digital;
    std::size_t a, b = 0;
};
struct sample {
    double time;
    std::vector<std::optional<double>> values;
    std::vector<bit_value> exact_bits;
};
struct obligation {
    std::string name;
    bool assumption = false, digital = false, final_only = true;
    std::size_t a = 0, b = 0;
    double low = 0, high = 0;
    std::uint64_t expected = 0;
    std::string status = "unverified";
};

class simulation {
    friend class IC;
    inline static thread_local simulation* current_ = nullptr;
    simulation* previous_;
    std::vector<std::size_t> parents_{0};
    std::vector<std::size_t> index_;
    std::vector<std::size_t> nodes_;
    std::vector<double> solution_;
    std::vector<observation> observations_;
    std::vector<obligation> obligations_;
    std::vector<std::vector<std::optional<double>>> obligation_samples_;
    bool frozen_ = false;
    std::string status_ = "unverified";
    std::size_t unknowns_ = 0;
    std::vector<component_info> components_;
    std::size_t active_component_ = no_component;
    std::vector<clocked_process> clocked_;

    std::size_t register_component() {
        editable();
        auto id = components_.size();
        components_.push_back({id, "IC" + std::to_string(id)});
        return id;
    }

    void compile();
    std::pair<std::vector<double>, std::vector<double>> residual(const std::vector<double>& x,
                                                                 const std::vector<double>& old, double dt, double time,
                                                                 bool hold, bool initial, bool right) const;
    std::vector<double> solve(std::vector<double> x, const std::vector<double>& old, double dt, double time,
                              bool hold = false, bool initial = false, bool right = true);
    std::vector<double> settle(std::vector<double> x, const std::vector<double>& old, double dt, double time,
                               bool hold = false, bool initial = false, bool right = true);
    void combinational();
    void stimuli(double time, bool right);
    void record(double time);
    double node_voltage(const std::vector<double>& x, std::size_t node) const {
        auto n = root(node);
        if (n == 0)
            return 0.0;
        if (n >= index_.size() || index_[n] == no_component)
            throw diagnostic("E_UNVERIFIED", "unused wire has no solved voltage");
        return x.at(index_[n]);
    }
    void editable() const {
        if (frozen_)
            throw diagnostic("E_ELABORATION", "declare purposes before integration starts");
    }

public:
    std::vector<element> elements;
    std::vector<bit_state> digital;
    std::vector<digital_operation> operations;
    std::vector<digital_stimulus> inputs;
    std::vector<sample> trace;
    std::size_t newton_iterations = 0, register_commits = 0;
    simulation() : previous_(current_) {
        current_ = this;
    }
    ~simulation() {
        current_ = previous_;
    }
    simulation(const simulation&) = delete;
    simulation& operator=(const simulation&) = delete;
    static simulation& current() {
        if (!current_)
            throw diagnostic("E_CONTEXT", "construct a simulation before declaring circuit variables");
        return *current_;
    }
    std::size_t wire() {
        editable();
        auto id = parents_.size();
        parents_.push_back(id);
        return id;
    }
    std::size_t root(std::size_t n) const {
        while (parents_.at(n) != n)
            n = parents_[n];
        return n;
    }
    void connect(const electrical& a, const electrical& b) {
        editable();
        if (&a.owner() != this || &b.owner() != this)
            throw diagnostic("E_CONTEXT", "wires belong to different simulations");
        auto aa = root(a.id()), bb = root(b.id());
        if (aa == bb)
            return;
        if (bb == 0)
            parents_[aa] = 0;
        else
            parents_[bb] = aa;
    }
    void reference(const electrical& a) {
        editable();
        if (&a.owner() != this)
            throw diagnostic("E_CONTEXT", "reference belongs to another simulation");
        parents_[root(a.id())] = 0;
    }
    void add(element e, std::source_location at = std::source_location::current()) {
        editable();
        e.file = at.file_name();
        e.line = at.line();
        e.component = active_component_;
        elements.push_back(std::move(e));
    }
    std::size_t digital_wire(unsigned width) {
        editable();
        auto id = digital.size();
        digital.push_back({width, std::nullopt, false});
        return id;
    }
    void writer(std::size_t id) {
        editable();
        if (digital.at(id).writer)
            throw diagnostic("E_DRIVER", "a bit signal has more than one purpose assigning it");
        digital[id].writer = true;
    }
    void add(digital_operation op) {
        writer(op.out);
        operations.push_back(op);
    }
    void observe(std::string name, const electrical& a, const electrical& b) {
        editable();
        check(a, b);
        observations_.push_back({std::move(name), false, a.id(), b.id()});
    }
    template <unsigned W> void observe(std::string name, const bits<W>& a);
    void check(const electrical& a, const electrical& b) const {
        if (&a.owner() != this || &b.owner() != this)
            throw diagnostic("E_CONTEXT", "cross-simulation terminal");
    }
    analog::voltage voltage(const electrical& a, const electrical& b) const {
        check(a, b);
        if (solution_.empty() && unknowns_ != 0)
            throw diagnostic("E_UNVERIFIED", "no solved voltage yet");
        return analog::voltage(node_voltage(solution_, a.id()) - node_voltage(solution_, b.id()));
    }
    void require_final_voltage(std::string name, const electrical& a, const electrical& b, analog::voltage low,
                               analog::voltage high) {
        editable();
        check(a, b);
        bounds(low.value, high.value);
        obligations_.push_back({std::move(name), false, false, true, a.id(), b.id(), low.value, high.value});
    }
    void assume_voltage(std::string name, const electrical& a, const electrical& b, analog::voltage low,
                        analog::voltage high) {
        editable();
        check(a, b);
        bounds(low.value, high.value);
        obligations_.push_back({std::move(name), true, false, false, a.id(), b.id(), low.value, high.value});
    }
    template <unsigned W> void require_final(std::string name, const bits<W>& a, std::uint64_t expected);
    template <unsigned W> void drive(const bits<W>& a, std::uint64_t value);
    template <unsigned W> void step(const bits<W>& a, duration at, std::uint64_t before, std::uint64_t after);
    void clock(const bits<1>& a, duration period);
    void register_clocked(const bits<1>& clock, const void* identity, std::function<void(bool)> work,
                          std::function<void()> commit);
    static void bounds(double low, double high) {
        if (!std::isfinite(low) || !std::isfinite(high) || low > high)
            throw diagnostic("E_INFEASIBLE", "empty or nonfinite interval");
    }
    void run(duration stop, duration step);
    const std::string& status() const {
        return status_;
    }
    const std::vector<obligation>& obligations() const {
        return obligations_;
    }
    const std::vector<component_info>& components() const {
        return components_;
    }
    void write(const std::filesystem::path& destination) const;
};

inline electrical::electrical() : owner_(&simulation::current()), id_(owner_->wire()) {}
inline electrical& electrical::operator=(const electrical& other) {
    owner_->connect(*this, other);
    return *this;
}

// C++HDL-style member ports. Reading a physical port yields a shared wire handle.
template <class T> class port : public T {
public:
    using value_type = T;
    using T::T;
    using T::operator=;
    port() = default;
    port(const port&) = default;
    port& operator=(const port& other) {
        T::operator=(static_cast<const T&>(other));
        return *this;
    }
    T& operator()() {
        return *this;
    }
    const T& operator()() const {
        return *this;
    }
};

// Common IC identity, inherited supply ports, and one-time declaration lifecycle.
class IC {
    simulation* owner_;
    std::size_t id_;

protected:
    IC() : owner_(&simulation::current()), id_(owner_->register_component()) {}

public:
    std::string __inst_name;

    // Ordinary per-instance wires: callers provide the actual supply connections.
    _PORT(electrical) Vdd;
    _PORT(electrical) Vss;

    virtual ~IC() = default;
    IC(const IC&) = delete;
    IC& operator=(const IC&) = delete;
    IC(IC&&) = delete;
    IC& operator=(IC&&) = delete;

    virtual void _assign() = 0;

    simulation& owner() const {
        return *owner_;
    }
    std::size_t id() const {
        return id_;
    }
    const std::string& name() const {
        return owner_->components_.at(id_).name;
    }
    void name(std::string value) {
        owner_->editable();
        __inst_name = value;
        owner_->components_.at(id_).name = std::move(value);
    }
    void elaborate() {
        owner_->editable();
        if (&simulation::current() != owner_)
            throw diagnostic("E_CONTEXT", "elaborate the IC in its owning simulation");
        auto state = owner_->components_.at(id_).state;
        if (state == component_state::ready)
            return; // Repeated requests must not duplicate device purposes.
        if (state != component_state::declared)
            throw diagnostic("E_ELABORATION", "recursive or previously failed IC declaration");
        owner_->components_[id_].state = component_state::assigning;
        auto previous = owner_->active_component_;
        owner_->active_component_ = id_;
        try {
            _assign(); // Virtual dispatch supplies this IC's line-by-line purposes.
            owner_->components_[id_].state = component_state::ready;
            owner_->active_component_ = previous;
        } catch (...) {
            owner_->components_[id_].state = component_state::failed;
            owner_->active_component_ = previous;
            throw;
        }
    }
};

inline void positive(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0)
        throw diagnostic("E_VALUE", std::string(name) + " must be finite and positive");
}
inline void finite(double value, const char* name) {
    if (!std::isfinite(value))
        throw diagnostic("E_VALUE", std::string(name) + " must be finite");
}
struct mos_model {
    channel_strength beta{.001}; // A/V^2: shared framework default, overridable per type/instance.
    analog::voltage threshold{.35};
};

// Device objects retain a physical terminal; conversion exposes it to a purpose.
class terminal_device {
protected:
    electrical terminal_;

public:
    operator const electrical&() const {
        return terminal_;
    }
    terminal_device() = default;
    terminal_device(const terminal_device&) = delete;
    terminal_device(terminal_device&&) = default;
};

class pmos : public terminal_device {
public:
    // Purpose: enable the returned output from source when gate is sufficiently low.
    pmos(const electrical& gate, const electrical& source, mos_model model = {},
         std::source_location at = std::source_location::current()) {
        terminal_.owner().check(gate, source);
        positive(model.beta.value, "beta");
        positive(model.threshold.value, "threshold");
        element e{law::pmos};
        e.gate = gate.id();
        e.a = terminal_.id();
        e.b = source.id();
        e.beta = model.beta.value;
        e.threshold = model.threshold.value;
        terminal_.owner().add(e, at);
    }
    static auto from(const electrical& source, mos_model model = {}) {
        // A normal C++ callable alias with a bound source terminal.
        return [source, model](const electrical& gate, std::source_location at = std::source_location::current()) {
            return pmos(gate, source, model, at);
        };
    }
};

class nmos : public terminal_device {
public:
    // Purpose: disable a drain output by conducting it toward the returned source.
    nmos(const electrical& gate, const electrical& drain, mos_model model = {},
         std::source_location at = std::source_location::current()) {
        terminal_.owner().check(gate, drain);
        positive(model.beta.value, "beta");
        positive(model.threshold.value, "threshold");
        element e{law::nmos};
        e.gate = gate.id();
        e.a = drain.id();
        e.b = terminal_.id();
        e.beta = model.beta.value;
        e.threshold = model.threshold.value;
        terminal_.owner().add(e, at);
    }
};

class resistor : public terminal_device {
public:
    resistor(const electrical& input, resistance r, std::source_location at = std::source_location::current()) {
        terminal_.owner().check(input, terminal_);
        positive(r.value, "resistance");
        element e{law::resistor};
        e.a = input.id();
        e.b = terminal_.id();
        e.r = r.value;
        terminal_.owner().add(e, at);
    }
};
class capacitor : public terminal_device {
public:
    capacitor(const electrical& reference, capacitance_value c, analog::voltage initial = analog::voltage(0),
              std::source_location at = std::source_location::current()) {
        terminal_.owner().check(reference, terminal_);
        positive(c.value, "capacitance");
        finite(initial.value, "initial voltage");
        element e{law::capacitor};
        e.a = terminal_.id();
        e.b = reference.id();
        e.c = c.value;
        e.initial = initial.value;
        terminal_.owner().add(e, at);
    }
};

// An active variable: its wire has charge-storage behavior as well as connectivity.
class capacitance : public electrical {
public:
    using electrical::operator=;
    capacitance(capacitance_value c, const electrical& reference, analog::voltage initial = analog::voltage(0),
                std::source_location at = std::source_location::current()) {
        owner().check(*this, reference);
        positive(c.value, "capacitance");
        finite(initial.value, "initial voltage");
        element e{law::capacitor};
        e.a = id();
        e.b = reference.id();
        e.c = c.value;
        e.initial = initial.value;
        owner().add(e, at);
    }
};

// Simulation-only ideal voltage stimulus; no physical source circuit is instantiated.
class sim_voltage_source : public terminal_device {
public:
    sim_voltage_source(const electrical& reference, analog::voltage value,
                       std::source_location at = std::source_location::current()) {
        terminal_.owner().check(reference, terminal_);
        if (!std::isfinite(value.value))
            throw diagnostic("E_VALUE", "nonfinite voltage");
        element e{law::sim_voltage_source};
        e.a = terminal_.id();
        e.b = reference.id();
        e.value = value.value;
        terminal_.owner().add(e, at);
    }
    sim_voltage_source(const electrical& reference, analog::voltage before, duration when, analog::voltage after,
                       std::source_location at = std::source_location::current()) {
        terminal_.owner().check(reference, terminal_);
        if (!std::isfinite(when.value) || when.value < 0 || !std::isfinite(before.value) || !std::isfinite(after.value))
            throw diagnostic("E_VALUE", "invalid source step");
        element e{law::sim_voltage_source};
        e.a = terminal_.id();
        e.b = reference.id();
        e.value = before.value;
        e.stepped = true;
        e.at = when.value;
        e.next = after.value;
        terminal_.owner().add(e, at);
    }
};

struct digital_purpose {
    simulation* owner;
    unsigned width;
    digital_operation operation;
};
template <unsigned W> class bits {
    static_assert(W >= 1 && W <= 64, "prototype bit widths are 1..64");
    simulation* owner_;
    std::size_t id_;

public:
    using cpphdl_connection_tag = void;
    template <class T> using cpphdl_port_type = analog::port<T>;
    bits() : owner_(&simulation::current()), id_(owner_->digital_wire(W)) {}
    bits(const bits&) = default;
    bits& operator=(digital_purpose p) {
        if (p.owner != owner_ || p.width != W)
            throw diagnostic("E_TYPE", "incompatible digital purpose");
        p.operation.out = id_;
        owner_->add(p.operation);
        return *this;
    }
    bits& operator=(const bits& input) {
        return *this = digital_purpose{&input.owner(), W, {digital_law::copy, 0, input.id()}};
    }
    simulation& owner() const {
        return *owner_;
    }
    std::size_t id() const {
        return id_;
    }
    bit_value value() const {
        return owner_->digital.at(id_).value;
    }
};

// Simulation-only bit operations; these do not instantiate gates or register circuitry.
template <unsigned W> digital_purpose sim_add(const bits<W>& a, const bits<W>& b) {
    if (&a.owner() != &b.owner())
        throw diagnostic("E_CONTEXT", "cross-simulation bits");
    return {&a.owner(), W, {digital_law::sim_add, 0, a.id(), b.id()}};
}
inline digital_purpose sim_invert(const bits<1>& a) {
    return {&a.owner(), 1, {digital_law::sim_invert, 0, a.id()}};
}
template <unsigned W> digital_purpose sim_store(const bits<W>& data, const bits<1>& clock, std::uint64_t initial = 0) {
    if (&data.owner() != &clock.owner())
        throw diagnostic("E_CONTEXT", "cross-simulation clock");
    if constexpr (W < 64)
        if (initial >= (std::uint64_t(1) << W))
            throw diagnostic("E_WIDTH", "initial register value");
    digital_operation op{digital_law::sim_store};
    op.a = data.id();
    op.clock = clock.id();
    op.initial = initial;
    return {&data.owner(), W, op};
}
// Ideal threshold interpretation with no input loading or physical sensing circuit.
inline digital_purpose sim_sense(const electrical& a, const electrical& reference, analog::voltage low,
                                 analog::voltage high) {
    a.owner().check(a, reference);
    simulation::bounds(low.value, high.value);
    if (low == high)
        throw diagnostic("E_VALUE", "threshold interval needs nonzero width");
    digital_operation op{digital_law::sim_sense};
    op.input = a.id();
    op.reference = reference.id();
    op.low = low.value;
    op.high = high.value;
    return {&a.owner(), 1, op};
}

// Simulation-only bit-to-rail path; resistance models loading, not an output-stage topology.
class sim_driver : public terminal_device {
public:
    sim_driver(const bits<1>& input, const electrical& vdd, const electrical& vss, resistance r,
               std::source_location at = std::source_location::current()) {
        terminal_.owner().check(vdd, vss);
        positive(r.value, "drive resistance");
        if (&input.owner() != &terminal_.owner())
            throw diagnostic("E_CONTEXT", "sim_driver bit in another simulation");
        element e{law::sim_driver};
        e.a = terminal_.id();
        e.vdd = vdd.id();
        e.vss = vss.id();
        e.bit = input.id();
        e.r = r.value;
        terminal_.owner().add(e, at);
    }
};
// Simulation-only conductance transfer approximation with ideal zero-current control terminals.
class sim_controlled_resistor : public terminal_device {
public:
    sim_controlled_resistor(const electrical& input, const electrical& control, const electrical& reference,
                            resistance r, analog::voltage low, analog::voltage high,
                            control_response response = control_response::rising,
                            std::source_location at = std::source_location::current()) {
        terminal_.owner().check(input, control);
        terminal_.owner().check(control, reference);
        positive(r.value, "on resistance");
        simulation::bounds(low.value, high.value);
        if (!(low < high))
            throw diagnostic("E_VALUE", "control interval needs low < high");
        element e{law::sim_controlled_resistor};
        e.a = input.id();
        e.b = terminal_.id();
        e.gate = control.id();
        e.reference = reference.id();
        e.r = r.value;
        e.low = low.value;
        e.high = high.value;
        e.falling = response == control_response::falling;
        terminal_.owner().add(e, at);
    }
};
template <unsigned W> void simulation::observe(std::string name, const bits<W>& a) {
    editable();
    if (&a.owner() != this)
        throw diagnostic("E_CONTEXT", "observation in another simulation");
    observations_.push_back({std::move(name), true, a.id()});
}
template <unsigned W> void simulation::drive(const bits<W>& a, std::uint64_t value) {
    if (&a.owner() != this)
        throw diagnostic("E_CONTEXT", "stimulus in another simulation");
    if constexpr (W < 64)
        if (value >= (std::uint64_t(1) << W))
            throw diagnostic("E_WIDTH", "stimulus value");
    writer(a.id());
    inputs.push_back({digital_stimulus::constant, a.id(), 0, 0, value, value});
}
template <unsigned W> void simulation::step(const bits<W>& a, duration at, std::uint64_t before, std::uint64_t after) {
    if (&a.owner() != this)
        throw diagnostic("E_CONTEXT", "stimulus in another simulation");
    if (!std::isfinite(at.value) || at.value < 0)
        throw diagnostic("E_VALUE", "invalid digital step time");
    if constexpr (W < 64)
        if (before >= (std::uint64_t(1) << W) || after >= (std::uint64_t(1) << W))
            throw diagnostic("E_WIDTH", "stimulus value");
    writer(a.id());
    inputs.push_back({digital_stimulus::step, a.id(), 0, at.value, before, after});
}
inline void simulation::clock(const bits<1>& a, duration period) {
    if (&a.owner() != this)
        throw diagnostic("E_CONTEXT", "clock in another simulation");
    positive(period.value, "clock period");
    writer(a.id());
    inputs.push_back({digital_stimulus::clock, a.id(), period.value});
}
inline void simulation::register_clocked(const bits<1>& clock, const void* identity, std::function<void(bool)> work,
                                         std::function<void()> commit) {
    editable();
    if (&clock.owner() != this)
        throw diagnostic("E_CONTEXT", "native module clock belongs to another simulation");
    if (!identity || !work || !commit)
        throw diagnostic("E_VALUE", "clocked process needs an identity and both lifecycle callbacks");
    for (const auto& process : clocked_)
        if (process.identity == identity)
            throw diagnostic("E_DRIVER", "native module is already registered with a clock");
    clocked_.push_back({clock.id(), identity, std::move(work), std::move(commit)});
}
template <unsigned W> void simulation::require_final(std::string name, const bits<W>& a, std::uint64_t expected) {
    editable();
    if (&a.owner() != this)
        throw diagnostic("E_CONTEXT", "requirement in another simulation");
    if constexpr (W < 64)
        if (expected >= (std::uint64_t(1) << W))
            throw diagnostic("E_WIDTH", "requirement value");
    obligation q;
    q.name = std::move(name);
    q.digital = true;
    q.a = a.id();
    q.expected = expected;
    obligations_.push_back(q);
}

} // namespace analog
#include "solver.hpp"

namespace cpphdl {
namespace analog = ::analog;
}
