#pragma once
#include "behavior.hpp"
#include <osdi.h> // Pinned external ABI header; see tests/IHPSG13G2/prepare_sdk.py.
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <numeric>

namespace analog::osdi {

inline std::string lower(std::string value) {
    for (auto& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

class storage {
    std::vector<std::max_align_t> words_;
    std::size_t size_;
public:
    explicit storage(std::size_t size = 0)
        : words_((size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t)), size_(size) {
        if (size_) std::memset(words_.data(), 0, size_);
    }
    void* data() { return words_.data(); }
    void restore(const storage& saved) noexcept {
        // Only used with the same instance allocation created during bind().
        std::memcpy(words_.data(), saved.words_.data(), size_);
    }
    template <class T> T* at(std::size_t offset, std::size_t count = 1) {
        if (offset > size_ || count > (size_ - offset) / sizeof(T) || offset % alignof(T))
            throw diagnostic("E_OSDI_ABI", "invalid compact-model storage offset");
        return reinterpret_cast<T*>(reinterpret_cast<char*>(data()) + offset);
    }
};

struct messages {
    bool error = false;
    std::string text;
    static void log(void* handle, char* message, uint32_t level) noexcept {
        auto* target = static_cast<messages*>(handle);
        if (target) {
            target->error |= (level & 7) >= LOG_LVL_ERR;
            try { if (target->text.size() < 4096) target->text += std::string(message) + '\n'; }
            catch (...) { target->error = true; }
        }
        if (!(level & LOG_FMT_ERR)) std::free(message);
    }
    void check(OsdiInitInfo& result) {
        const bool failed = result.num_errors || result.flags || error;
        std::free(result.errors);
        result.errors = nullptr;
        if (failed) throw diagnostic("E_OSDI_SETUP", "compact-model initialization failed: " + text);
    }
};

class library {
    void* handle_ = nullptr;
public:
    const OsdiDescriptor* descriptor = nullptr;
    std::map<std::string, uint32_t> parameter_ids;
    library(const std::filesystem::path& path, const std::string& module) {
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_) throw diagnostic("E_OSDI_LOAD", dlerror());
        try {
            auto symbol = [&](const char* name) {
                auto* value = dlsym(handle_, name);
                if (!value) throw diagnostic("E_OSDI_ABI", std::string("missing symbol ") + name);
                return value;
            };
            if (*static_cast<uint32_t*>(symbol("OSDI_VERSION_MAJOR")) != 0 ||
                *static_cast<uint32_t*>(symbol("OSDI_VERSION_MINOR")) != 3)
                throw diagnostic("E_OSDI_ABI", "this adapter requires OSDI 0.3");
            auto* descriptors = static_cast<OsdiDescriptor*>(symbol("OSDI_DESCRIPTORS"));
            const auto count = *static_cast<uint32_t*>(symbol("OSDI_NUM_DESCRIPTORS"));
            for (uint32_t i = 0; i < count; ++i)
                if (lower(descriptors[i].name) == lower(module)) descriptor = &descriptors[i];
            if (!descriptor) throw diagnostic("E_OSDI_MODEL", "module is absent: " + module);
            for (uint32_t i = 0; i < descriptor->num_params; ++i) {
                const auto& parameter = descriptor->param_opvar[i];
                for (uint32_t alias = 0; alias <= parameter.num_alias; ++alias) {
                    const auto [entry, inserted] = parameter_ids.emplace(lower(parameter.name[alias]), i);
                    if (!inserted && entry->second != i)
                        throw diagnostic("E_OSDI_PARAMETER", "ambiguous compact-model parameter alias");
                }
            }
            if (auto* log = dlsym(handle_, "osdi_log"))
                *static_cast<decltype(&messages::log)*>(log) = &messages::log;
            // Device limiting is deliberately disabled. The native damped
            // Newton solver evaluates the unmodified equations and Jacobian.
            if (auto* count_ptr = dlsym(handle_, "OSDI_LIM_TABLE_LEN")) {
                auto* table = static_cast<OsdiLimFunction*>(symbol("OSDI_LIM_TABLE"));
                for (uint32_t i = 0; i < *static_cast<uint32_t*>(count_ptr); ++i) table[i].func_ptr = nullptr;
            }
        } catch (...) { dlclose(handle_); handle_ = nullptr; throw; }
    }
    library(const library&) = delete;
    library& operator=(const library&) = delete;
    ~library() { if (handle_) dlclose(handle_); }
};

struct simulation_parameters {
    char* names[11] = {const_cast<char*>("iniLim"), const_cast<char*>("gmin"), const_cast<char*>("gdev"),
        const_cast<char*>("tnom"), const_cast<char*>("simulatorVersion"), const_cast<char*>("sourceScaleFactor"),
        const_cast<char*>("epsmin"), const_cast<char*>("reltol"), const_cast<char*>("vntol"),
        const_cast<char*>("abstol"), nullptr};
    double values[10] = {0, 1e-12, 1e-12, 27, 47, 1, 1e-28, 1e-6, 1e-9, 1e-12};
    char* strings[1] = {nullptr};
    OsdiSimParas get() { return {names, values, strings, nullptr}; }
};

inline void set_parameter(const library& binary, void* inst, void* model,
                          const std::string& name, double value, bool instance) {
    if (!std::isfinite(value)) throw diagnostic("E_OSDI_PARAMETER", "nonfinite parameter: " + name);
    const auto entry = binary.parameter_ids.find(lower(name));
    if (entry == binary.parameter_ids.end()) throw diagnostic("E_OSDI_PARAMETER", "unknown parameter: " + name);
    const auto& d = *binary.descriptor;
    const auto i = entry->second;
    const auto& p = d.param_opvar[i];
    // Instance parameters may also supply defaults on a model card.
    if ((instance && i >= d.num_instance_params) || p.len != 0)
        throw diagnostic("E_OSDI_PARAMETER", "wrong parameter scope or unsupported array: " + name);
    auto* destination = d.access(inst, model, i, ACCESS_FLAG_SET | (instance ? ACCESS_FLAG_INSTANCE : 0));
    if (!destination) throw diagnostic("E_OSDI_PARAMETER", "parameter is not writable: " + name);
    switch (p.flags & PARA_TY_MASK) {
    case PARA_TY_REAL: *static_cast<double*>(destination) = value; return;
    case PARA_TY_INT:
        if (std::floor(value) != value || value < INT32_MIN || value > INT32_MAX)
            throw diagnostic("E_OSDI_PARAMETER", "integer required: " + name);
        *static_cast<int32_t*>(destination) = int32_t(value); return;
    default: throw diagnostic("E_OSDI_PARAMETER", "numeric parameter required: " + name);
    }
}

class model {
public:
    std::shared_ptr<library> binary;
    storage data;
    simulation_parameters simulation;
    model(std::shared_ptr<library> lib, const std::map<std::string, double>& parameters)
        : binary(std::move(lib)), data(binary->descriptor->model_size) {
        const auto& d = *binary->descriptor;
        for (const auto& [name, value] : parameters) set_parameter(*binary, nullptr, data.data(), name, value, false);
        messages log;
        OsdiInitInfo result{};
        auto params = simulation.get();
        d.setup_model(&log, data.data(), &params, &result);
        log.check(result);
    }
    model(const model&) = delete;
    model& operator=(const model&) = delete;
};

// An electrical IC contributes all its terminal/internal F, Q, dF and dQ
// equations to the native coupled solver. No ngspice solver is called here.
class device : public behavior::stateful_device {
    storage instance_, checkpoint_;
    std::vector<std::size_t> global_;
    std::vector<uint32_t> local_;
    std::vector<double> solution_, current_, charge_, conductance_, capacitance_;
    std::vector<double> committed_, pending_;
    std::shared_ptr<model> bound_process_;
    std::map<std::string, double> bound_parameters_;
    double bound_temperature_ = 0;
    bool transient_ = false, bound_ = false;
protected:
    virtual std::vector<electrical> terminals() const = 0;
public:
    std::shared_ptr<model> process;
    std::map<std::string, double> parameters;
    double temperature_K = 300.15;
    std::size_t evaluations = 0;
    std::vector<std::pair<std::string, std::size_t>> node_mapping() const {
        if (!bound_) throw diagnostic("E_OSDI_SETUP", "unbound compact device");
        std::vector<std::pair<std::string, std::size_t>> result;
        const auto& d = *bound_process_->binary->descriptor;
        for (uint32_t i = 0; i < d.num_nodes; ++i) result.emplace_back(d.nodes[i].name, global_.at(local_[i]));
        return result;
    }
    behavior::capabilities features() const override {
        return {.charge = true, .rollback = true, .compact_model = true};
    }
    void bind(behavior::binding& layout) override {
        if (bound_ || !process || !std::isfinite(temperature_K) || temperature_K <= 0)
            throw diagnostic("E_OSDI_SETUP", "invalid or repeated device binding");
        const auto& d = *process->binary->descriptor;
        if (d.bound_step_offset != UINT32_MAX)
            throw diagnostic("E_OSDI_UNSUPPORTED", "models requesting $bound_step require a step-control adapter");
        const auto ports = terminals();
        if (ports.size() != d.num_terminals) throw diagnostic("E_OSDI_SETUP", "wrong terminal count");
        instance_ = storage(d.instance_size);
        for (const auto& [name, value] : parameters)
            set_parameter(*process->binary, instance_.data(), process->data.data(), name, value, true);
        auto params = process->simulation.get();
        messages log;
        OsdiInitInfo result{};
        d.setup_instance(&log, instance_.data(), process->data.data(), temperature_K, d.num_terminals, &params, &result);
        log.check(result);
        std::vector<uint32_t> parent(d.num_nodes + 1);
        std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](uint32_t i) { while (parent.at(i) != i) i = parent.at(i); return i; };
        if (d.num_collapsible) {
            auto* collapsed = instance_.at<bool>(d.collapsed_offset, d.num_collapsible);
            for (uint32_t i = 0; i < d.num_collapsible; ++i) {
                if (!collapsed[i]) continue;
                auto a = root(d.collapsible[i].node_1);
                auto b = root(d.collapsible[i].node_2 == UINT32_MAX ? d.num_nodes : d.collapsible[i].node_2);
                if (a == b) continue;
                const auto external = [&](uint32_t node) { return node < d.num_terminals || node == d.num_nodes; };
                if (external(a) && external(b)) continue;
                if (a < d.num_terminals || a == d.num_nodes || (b >= d.num_terminals && b < a)) std::swap(a, b);
                parent[a] = b;
            }
        }
        global_.assign(d.num_nodes + 1, behavior::ground);
        local_.resize(d.num_nodes);
        for (uint32_t i = 0; i < d.num_terminals; ++i) global_[i + 1] = layout.node(ports[i]);
        for (uint32_t i = d.num_terminals; i < d.num_nodes; ++i)
            if (root(i) == i) {
                // Internal potentials have KCL rows; branch flows have voltage
                // constraint rows, matching the native solver's residual units.
                global_[i + 1] = layout.voltage_equation.size();
                layout.voltage_equation.push_back(d.nodes[i].is_flow);
            }
        auto* mapping = instance_.at<uint32_t>(d.node_mapping_offset, d.num_nodes);
        for (uint32_t i = 0; i < d.num_nodes; ++i) mapping[i] = local_[i] = root(i) == d.num_nodes ? 0 : root(i) + 1;
        solution_.resize(d.num_nodes + 1);
        current_.resize(d.num_nodes + 1);
        charge_.resize(d.num_nodes + 1);
        conductance_.resize(d.num_jacobian_entries);
        capacitance_.resize(d.num_jacobian_entries);
        auto** pointers = instance_.at<double*>(d.jacobian_ptr_resist_offset, d.num_jacobian_entries);
        for (uint32_t i = 0; i < d.num_jacobian_entries; ++i) {
            pointers[i] = &conductance_[i];
            const auto offset = d.jacobian_entries[i].react_ptr_off;
            if (offset != UINT32_MAX) *instance_.at<double*>(offset) = &capacitance_[i];
        }
        committed_.resize(d.num_states);
        pending_.resize(d.num_states);
        if (d.num_states) {
            auto* states = instance_.at<uint32_t>(d.state_idx_off, d.num_states);
            std::iota(states, states + d.num_states, 0);
        }
        checkpoint_ = instance_;
        bound_process_ = process;
        bound_parameters_ = parameters;
        bound_temperature_ = temperature_K;
        bound_ = true;
    }
    void begin_step(double from, double to) override {
        transient_ = to > from;
        checkpoint_ = instance_;
        pending_ = committed_;
    }
    void accept() noexcept override { std::copy(pending_.begin(), pending_.end(), committed_.begin()); }
    void reject() noexcept override {
        instance_.restore(checkpoint_);
        std::copy(committed_.begin(), committed_.end(), pending_.begin());
    }
    void evaluate(const behavior::evaluation& state, behavior::equations& out) override {
        if (!bound_) throw diagnostic("E_OSDI_SETUP", "unbound compact device");
        if (process != bound_process_ || parameters != bound_parameters_ || temperature_K != bound_temperature_)
            throw diagnostic("E_OSDI_SETUP", "process parameters changed after device binding");
        const auto& d = *process->binary->descriptor;
        for (std::size_t i = 0; i < global_.size(); ++i) solution_[i] = state.value(global_[i]);
        pending_ = committed_;
        OsdiSimInfo info{process->simulation.get(), state.time, solution_.data(), committed_.data(), pending_.data(),
            uint32_t(CALC_RESIST_RESIDUAL | CALC_REACT_RESIDUAL | CALC_RESIST_JACOBIAN | CALC_REACT_JACOBIAN | CALC_OP |
            (transient_ ? ANALYSIS_TRAN : (ANALYSIS_DC | ANALYSIS_STATIC)))};
        messages log;
        const auto flags = d.eval(&log, instance_.data(), process->data.data(), &info);
        ++evaluations;
        if (flags || log.error) throw diagnostic("E_OSDI_EVAL", "compact-model evaluation rejected: " + log.text);
        std::fill(current_.begin(), current_.end(), 0);
        std::fill(charge_.begin(), charge_.end(), 0);
        std::fill(conductance_.begin(), conductance_.end(), 0);
        std::fill(capacitance_.begin(), capacitance_.end(), 0);
        d.load_residual_resist(instance_.data(), process->data.data(), current_.data());
        d.load_residual_react(instance_.data(), process->data.data(), charge_.data());
        d.load_jacobian_resist(instance_.data(), process->data.data());
        d.load_jacobian_react(instance_.data(), process->data.data(), 1);
        for (std::size_t i = 1; i < global_.size(); ++i) out.add(global_[i], current_[i], charge_[i]);
        for (uint32_t i = 0; i < d.num_jacobian_entries; ++i) {
            const auto pair = d.jacobian_entries[i].nodes;
            out.derivative(global_.at(local_.at(pair.node_1)), global_.at(local_.at(pair.node_2)),
                           conductance_[i], capacitance_[i]);
        }
    }
};

} // namespace analog::osdi

namespace analog::behavior {
template <> struct traits<osdi::device> : traits<stateful_device> {
    static constexpr capabilities features{.charge = true, .rollback = true, .compact_model = true};
    static constexpr std::string_view name = "OSDI compact device";
};
}
