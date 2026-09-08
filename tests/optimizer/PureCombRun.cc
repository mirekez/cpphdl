#include "PureCombRoot.h"
#include "PureCombRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    PureCombRoot root;
    cpphdl::logic<8> input = 4;
    root.input = _ASSIGN(input);
    root._assign();
    bind_optimized_ports(root);

    calc_all(root, false);
    if (uint64_t(root.output()) != 7) return 1;

    input = 20;
    ++_system_clock;
    calc_all(root, false);
    if (uint64_t(root.output()) != 23) return 2;
    return 0;
}
