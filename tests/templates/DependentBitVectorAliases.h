#include <cpphdl.h>
using namespace cpphdl;

// Conversion must work without a concrete C++ instantiation. Numeric widths
// belong to the SV module parameters, not to a specialized type name.
template<unsigned AXI_DATA_WIDTH = 64>
class DependentBitVectorAliases : public Module {
public:
    static constexpr unsigned AXI_STRB_WIDTH = AXI_DATA_WIDTH / 8;
    using data_t = logic<AXI_DATA_WIDTH>;
    using strb_t = logic<AXI_STRB_WIDTH>;
    using expr_t = logic<AXI_DATA_WIDTH / 8>;
    using chained_t = data_t;
    using unsigned_t = u<AXI_STRB_WIDTH>;
    _PORT(data_t) data;
    _PORT(strb_t) strb;
};

#ifdef DEPENDENT_ALIAS_NATIVE
#include <type_traits>
template<unsigned WIDTH>
constexpr bool check_aliases() {
    using DUT = DependentBitVectorAliases<WIDTH>;
    static_assert(std::is_same_v<typename DUT::data_t, logic<WIDTH>>);
    static_assert(std::is_same_v<typename DUT::strb_t, logic<WIDTH / 8>>);
    static_assert(std::is_same_v<typename DUT::expr_t, logic<WIDTH / 8>>);
    static_assert(std::is_same_v<typename DUT::chained_t, logic<WIDTH>>);
    static_assert(std::is_same_v<typename DUT::unsigned_t, u<WIDTH / 8>>);
    return true;
}
static_assert(check_aliases<32>() && check_aliases<64>() && check_aliases<128>());
static_assert(std::is_same_v<DependentBitVectorAliases<>::data_t, logic<64>>);
int main() { return 0; }
#endif
