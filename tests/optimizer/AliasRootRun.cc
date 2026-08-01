#include "AliasRoot.h"
#include "AliasRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    AliasRoot root;
    cpphdl::logic<8> input = 7;
    root.input = _ASSIGN_REG(input);
    bind_optimized_ports(root);

    calc_all(root, true);
    commit_optimized_regs(root);
    ++_system_clock;
    calc_all(root, false);
    commit_optimized_regs(root);
    ++_system_clock;

    return root.output() == cpphdl::logic<8>(10) ? 0 : 1;
}
