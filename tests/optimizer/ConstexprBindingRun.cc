#include "ConstexprBindingRoot.h"
#include "ConstexprBindingRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    ConstexprBindingRoot root;
    root._assign();
    calc_all(root, true);
    if ((uint64_t)(root.reset_seen) != 1) {
        return 1;
    }
    ++_system_clock;
    calc_all(root, false);
    return (uint64_t)(root.disabled_output()) == 0 &&
                   (uint64_t)(root.enabled_output()) == 0x5a &&
                   (uint64_t)(root.reset_seen) == 0
               ? 0
               : 1;
}
