#include "OpaqueSubtree.h"
#include "OpaqueSubtreeRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    OpaqueSubtreeRoot root;
    cpphdl::logic<1> input = 1;
    root.input = _ASSIGN(input);
    root._assign();
    bind_optimized_ports(root);
    if (root.child.assign_calls != 1) {
        return 1;
    }

    calc_all(root, false);
    if ((uint64_t)root.observed != 0 || root.child.assign_calls != 1) {
        return 2;
    }
    commit_optimized_regs(root);
    ++_system_clock;

    calc_all(root, false);
    if ((uint64_t)root.observed != 1 || root.child.assign_calls != 1) {
        return 3;
    }
    return 0;
}
