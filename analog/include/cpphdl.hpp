#pragma once
#include <cpphdl.h>
#include "analog.hpp"
#include <memory>

namespace analog {

// Simulation-only host adapters; native module execution does not add physical circuitry.

// Reject unknown analog interpretations at the boundary to C++HDL's two-state values.
template <unsigned W> cpphdl::logic<W> sim_cpphdl_value(const bits<W>& signal) {
    if (&signal.owner() != &simulation::current())
        throw diagnostic("E_CONTEXT", "native input belongs to another simulation");
    auto value = signal.value();
    if (!value)
        throw diagnostic("E_UNKNOWN_CPPHDL", "unknown signal cannot become a two-state C++HDL value");
    return cpphdl::logic<W>(*value);
}

template <unsigned W> auto sim_to_cpphdl(const bits<W>& signal) {
    if (&signal.owner() != &simulation::current())
        throw diagnostic("E_CONTEXT", "native input belongs to another simulation");
    return [signal] { return sim_cpphdl_value(signal); };
}

// Only registered outputs belong here: they cannot depend combinationally on inputs.
template <unsigned W, class Module>
digital_purpose sim_registered_output(std::shared_ptr<Module> module,
                                      cpphdl::function_ref<cpphdl::logic<W>> Module::* output) {
    static_assert(W >= 1 && W <= 64, "analog bridge widths are 1..64");
    static_assert(std::is_base_of_v<cpphdl::Module, Module>);
    if (!module || !output)
        throw diagnostic("E_VALUE", "native output needs a module and output port");
    digital_operation op{digital_law::sim_registered_output};
    op.native_identity = module.get();
    op.read = [module = std::move(module), output]() -> bit_value {
        auto& port = module.get()->*output;
        if (!port.assigned)
            throw diagnostic("E_UNDERCONSTRAINED", "native output port is unbound");
        ++_system_clock; // Invalidate native lazy-port caches; this is not physical time.
        return static_cast<std::uint64_t>(port());
    };
    return {&simulation::current(), W, std::move(op)};
}

template <class Module> void sim_clock_cpphdl(std::shared_ptr<Module> module, const bits<1>& clock) {
    static_assert(std::is_base_of_v<cpphdl::Module, Module>);
    if (!module)
        throw diagnostic("E_VALUE", "native clock needs a module");
    if (&clock.owner() != &simulation::current())
        throw diagnostic("E_CONTEXT", "native clock belongs to another simulation");
    module->_assign();
    clock.owner().register_clocked(
        clock, module.get(),
        [module](bool reset) {
            ++_system_clock;
            module->_work(reset);
        },
        [module] { module->_strobe(); });
}

} // namespace analog
