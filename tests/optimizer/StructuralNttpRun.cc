#include "StructuralNttpRoot.h"
#include "StructuralNttpRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    StructuralNttpRoot root;
    root.input = _ASSIGN(cpphdl::logic<3>(5));
    root._assign();
    calc_all(root, false);
    return static_cast<uint64_t>(root.work_value) == 5 &&
                   static_cast<uint64_t>(root.projected_value) == 5 &&
                   static_cast<uint64_t>(root.array_projected_value) == 5
               ? 0
               : 1;
}
