#include "ModuleArrayCallRoot.h"
#include "ModuleArrayCallRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    ModuleArrayCallRoot root;
    cpphdl::logic<1> input = 1;
    cpphdl::logic<1> select = 0;
    root.input = _ASSIGN(input);
    root.select = _ASSIGN(select);
    root._assign();
    calc_all(root, false);
    if ((uint64_t)root.work_value != 1 ||
        (uint64_t)root.constant_output() != 1 ||
        (uint64_t)root.port_constant_output() != 1) {
        return 1;
    }
    input = 0;
    select = 1;
    ++_system_clock;
    calc_all(root, false);
    return (uint64_t)root.work_value == 0 &&
                   (uint64_t)root.constant_output() == 0 &&
                   (uint64_t)root.port_constant_output() == 0
               ? 0
               : 2;
}
