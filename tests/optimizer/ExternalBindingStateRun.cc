#include "ExternalBindingState.h"
#include "ExternalBindingStateRoot_optimized_combs.h"
#include <cassert>
long _system_clock = 0;
namespace firtool_cpphdl_external {
void work(ExternalBindingLeaf& leaf, bool) { leaf.observed = leaf.input(); }
}
int main() {
    ExternalBindingStateRoot root;
    cpphdl::logic<8> input = 0;
    root.input = [&]() { return &input; };
    bind_optimized_ports(root);
    for (unsigned cycle = 1; cycle <= 5; ++cycle) {
        input = cycle * 3;
        ++_system_clock;
        calc_all(root);
        assert(static_cast<unsigned>(root.leaf.observed) == cycle * 3 + 1);
        assert(root.evaluations == cycle);
    }
}
