#include "ModuleArrayCallRoot.h"
#include "ModuleArrayCallRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    ModuleArrayCallRoot root;
    cpphdl::logic<1> input = 1;
    root.input = _ASSIGN(input);
    root._assign();
    calc_all(root, false);
    if ((uint64_t)root.work_value != 1) {
        return 1;
    }
    input = 0;
    ++_system_clock;
    calc_all(root, false);
    return (uint64_t)root.work_value == 0 ? 0 : 2;
}
