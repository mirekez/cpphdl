#include "ProjectedCombRoot.h"
#include "ProjectedCombRoot_optimized_combs.h"

#include <cstdint>

long _system_clock = 0;

int main()
{
    ProjectedCombRoot root;
    cpphdl::logic<8> input = 5;
    root.input = _ASSIGN(input);
    root._assign();
    bind_optimized_ports(root);

    calc_all(root, false);
    if ((uint64_t)root.op() != 8 || (uint64_t)root.result() != 0x5f ||
        (uint64_t)root.single() != 6 ||
        (uint64_t)root.array_op()[0] != 5 ||
        (uint64_t)root.array_op()[1] != 6)
        return 1;

    input = 8;
    ++_system_clock;
    calc_all(root, false);
    if ((uint64_t)root.op() != 6 || (uint64_t)root.result() != 0x88 ||
        (uint64_t)root.single() != 9 ||
        (uint64_t)root.array_op()[0] != 8 ||
        (uint64_t)root.array_op()[1] != 9)
        return 2;
    return 0;
}
