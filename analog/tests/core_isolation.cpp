#include <analog.hpp>
#include <type_traits>

template <class T, class = void> struct is_complete : std::false_type {};
template <class T> struct is_complete<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};

template <class T, class = void> struct has_strobe : std::false_type {};
template <class T> struct has_strobe<T, std::void_t<decltype(std::declval<T&>()._strobe())>> : std::true_type {};

// Including the analog core must not load native lazy ports or digital lifecycle methods.
static_assert(!is_complete<cpphdl::function_ref<int>>::value);
static_assert(!has_strobe<analog::IC>::value);
static_assert(std::is_same_v<_PORT(analog::electrical), analog::port<analog::electrical>>);

class Divider : public analog::IC {
public:
    _PORT(analog::electrical) Y;

    void _assign() override {
        using namespace analog::literals;
        Y = analog::resistor(Vdd, 1_kOhm);
        Vss = analog::resistor(Y, 1_kOhm);
    }
};

int main() {
    using namespace analog::literals;
    analog::simulation sim;
    Divider divider;
    divider.name("divider");
    sim.reference(divider.Vss);
    divider.Vdd = analog::sim_voltage_source(divider.Vss, 1_V);
    divider.elaborate();
    sim.observe("Y", divider.Y, divider.Vss);
    sim.run(1_us, 100_ns);
    const auto voltage = sim.trace.back().values[0];
    return divider.__inst_name == "divider" && voltage && std::abs(*voltage - 0.5) < 1e-9 ? 0 : 1;
}
